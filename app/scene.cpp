#include "scene.h"
#include "engine.h"
#include "screenshot.h"
#include "transmission.h"

#include <vkwave/core/renderdoc.h>
#include <vkwave/core/swapchain.h>

#include <vulkan/vulkan_to_string.hpp>

#include <imgui.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>

#include <glm/gtc/matrix_transform.hpp>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Scene::Scene(Engine& engine)
  : m_engine(&engine)
{
}

void Scene::build_pipeline()
{
  pipeline = std::make_unique<ScenePipeline>(*m_engine, data, vk::SampleCountFlagBits::e1);
  wire_pbr_context();
  wire_record_callbacks();
}

Scene::~Scene()
{
  if (screenshot_thread.joinable())
    screenshot_thread.join();
  m_engine->device->device().waitIdle();
}

// ---------------------------------------------------------------------------
// Wiring helpers
// ---------------------------------------------------------------------------

void Scene::wire_pbr_context()
{
  pbr_ctx.group = &pipeline->pbr_group();
  pbr_ctx.mesh = data.active_mesh();
  pbr_ctx.has_transparent = false;

  if (data.has_multi_material())
  {
    pbr_ctx.primitives = data.gltf_scene.primitives.data();
    pbr_ctx.primitive_count = static_cast<uint32_t>(data.gltf_scene.primitives.size());
    pbr_ctx.materials = data.gltf_scene.materials.data();
    pbr_ctx.material_count = static_cast<uint32_t>(data.gltf_scene.materials.size());

    for (auto& mat : data.gltf_scene.materials)
    {
      if (mat.alphaMode == vkwave::AlphaMode::Blend)
      {
        pbr_ctx.has_transparent = true;
        break;
      }
    }
  }
  else
  {
    pbr_ctx.primitives = nullptr;
    pbr_ctx.primitive_count = 0;
    pbr_ctx.materials = nullptr;
    pbr_ctx.material_count = 0;
  }

  pbr_pass.ctx = &pbr_ctx;
  blend_pass.ctx = &pbr_ctx;
  composite_pass.group = &pipeline->composite_group();

  // Transmission shares the scene context; its group is present only for glass.
  transmission_pass.ctx = &pbr_ctx;
  transmission_pass.group = pipeline->transmission_group();

  // When the transmission pass is present it owns transmissive prims, so the
  // opaque/blend passes must skip them (else they write depth + pollute the
  // snapshot). Without the pass (e.g. MSAA on), glass falls back to opaque.
  pbr_ctx.defer_transmissive = (transmission_pass.group != nullptr);
}

void Scene::wire_record_callbacks()
{
  pipeline->pbr_group().set_record_fn(
    [this](vk::CommandBuffer cmd, uint32_t /*frame_index*/) {
      pbr_pass.record(cmd);
      blend_pass.record(cmd);
    });

  // Screenshot copy: capture the HDR into the readback buffer and arm the
  // per-slot fence on `group` (the submission that contains the copy). Must run
  // on the LAST offscreen group that writes the HDR so the glass pass is
  // included — otherwise a glass scene would screenshot the pre-transmission HDR.
  auto record_screenshot = [this](vk::CommandBuffer cmd, vkwave::ExecutionGroup& group) {
    if (!screenshot_requested || screenshot_in_flight || screenshot_compressing)
      return;
    if (!screenshot_readback)
      return; // buffer not yet allocated — will be ready next frame

    auto extent = group.extent();
    vk::DeviceSize needed = static_cast<vk::DeviceSize>(extent.width) * extent.height * 8;
    if (screenshot_readback->size() < needed)
      return; // buffer too small — will grow next frame

    auto slot = m_engine->graph->last_offscreen_slot();
    auto hdr_image = m_engine->graph->resources().color_image(pipeline->hdr_handle, slot);
    record_hdr_screenshot_copy(cmd, hdr_image, extent, screenshot_readback->buffer());

    // Arm the fence — only this copy is serialized, frames keep pipelining
    screenshot_fence->reset();
    group.set_next_fence(screenshot_fence->get());

    screenshot_requested = false;
    screenshot_in_flight = true;
    screenshot_extent = extent;
    screenshot_format = ScenePipeline::kHdrFormat;
  };

  const bool has_transmission = (pipeline->transmission_group() != nullptr);

  // PBR post-record: snapshot copy (glass), then the screenshot copy *only* when
  // there is no transmission pass (otherwise the screenshot runs after glass).
  // Runs after endRenderPass(), before cmd.end(), same command buffer — no extra
  // vkQueueSubmit.
  pipeline->pbr_group().set_post_record_fn(
    [this, record_screenshot, has_transmission](vk::CommandBuffer cmd, uint32_t /*slot_index*/) {
      // Transmission snapshot: copy the opaque HDR into the per-slot snapshot the
      // refraction pass samples. Only when the transmission group is present to
      // consume it (the snapshot resource may exist at MSAA with no group).
      if (pipeline->transmission_group() && pipeline->snapshot_handle)
      {
        auto slot = m_engine->graph->last_offscreen_slot();
        auto& pool = m_engine->graph->resources();
        record_transmission_snapshot_copy(cmd,
          pool.color_image(pipeline->hdr_handle, slot),
          pool.color_image(*pipeline->snapshot_handle, slot),
          pipeline->pbr_group().extent());
      }

      if (!has_transmission)
        record_screenshot(cmd, pipeline->pbr_group());
    });

  // Transmission group (present only for glass scenes): draws refractive
  // primitives into the HDR target in its own submission. Its post-record holds
  // the screenshot copy so a captured glass scene includes the transmission.
  if (auto* tr = pipeline->transmission_group())
  {
    tr->set_record_fn(
      [this](vk::CommandBuffer cmd, uint32_t /*frame_index*/) {
        transmission_pass.record(cmd);
      });
    tr->set_post_record_fn(
      [this, record_screenshot](vk::CommandBuffer cmd, uint32_t /*slot_index*/) {
        record_screenshot(cmd, *pipeline->transmission_group());
      });
  }

  pipeline->composite_group().set_record_fn(
    [this](vk::CommandBuffer cmd, uint32_t frame_index) {
      auto slot = m_engine->graph->last_offscreen_slot();
      pipeline->composite_group().write_image_descriptor(
        0, "hdrImage", frame_index,
        m_engine->graph->resources().color_view(pipeline->hdr_handle, slot),
        pipeline->hdr_sampler);
      composite_pass.record(cmd);
    });

  // Composite post-record: ImGui overlay only
  auto* overlay = pipeline->imgui_overlay();
  pipeline->composite_group().set_post_record_fn(
    [overlay](vk::CommandBuffer cmd, uint32_t slot) {
      overlay->record(cmd, slot);
    });
}

// ---------------------------------------------------------------------------
// Runtime switching
// ---------------------------------------------------------------------------

void Scene::switch_model(const std::string& model_path)
{
  m_engine->graph->drain();
  data.load_model(*m_engine->device, model_path);

  // Fit camera to new model bounds
  if (data.gltf_scene.bounds.valid())
  {
    float bounds[6];
    data.gltf_scene.bounds.to_bounds(bounds);
    data.camera.reset_camera(bounds);
  }

  // If the new model crosses the glass boundary (transmission present <-> absent)
  // the *pass set* changes — structurally rebuild the graph (adds/removes the
  // transmission pass + snapshot) and re-wire callbacks. Otherwise the structure
  // is unchanged, so the lighter descriptor-only rebuild suffices.
  const bool want_transmission =
    data.has_transmission() &&
    pipeline->msaa_samples == vk::SampleCountFlagBits::e1;
  if (want_transmission != pipeline->has_transmission_pass())
  {
    pipeline->rebuild_graph(data);   // drains internally
    wire_pbr_context();
    wire_record_callbacks();
  }
  else
  {
    wire_pbr_context();
    pipeline->rebuild_pbr_descriptors(data);
  }
}

void Scene::switch_ibl(const std::string& hdr_path)
{
  m_engine->graph->drain();

  const bool capture = capture_next_ibl_reload && vkwave::RenderDoc::is_attached();
  capture_next_ibl_reload = false;
  if (capture)
    vkwave::RenderDoc::begin_capture();

  data.load_ibl(*m_engine->device, hdr_path);
  pipeline->write_ibl_descriptors(data);

  if (capture)
  {
    // EndFrameCapture submits its own work — make sure all IBL compute is
    // retired before we close the capture scope.
    m_engine->graph->drain();
    vkwave::RenderDoc::end_capture();
  }
}

void Scene::rebuild_pipeline(vk::SampleCountFlagBits new_samples)
{
  m_engine->graph->drain();
  pipeline->rebuild_for_msaa(new_samples, data);
  wire_pbr_context();
  wire_record_callbacks();
}

void Scene::resize(const vkwave::Swapchain& swapchain)
{
  pipeline->resize(swapchain, data);
}

// ---------------------------------------------------------------------------
// Screenshot readback buffer (grow-only, persistent)
// ---------------------------------------------------------------------------

void Scene::ensure_screenshot_readback(vk::DeviceSize needed)
{
  if (screenshot_readback && screenshot_readback->size() >= needed)
    return;

  // Grow-only: old buffer is not in use (fence was signaled or never used).
  screenshot_readback = std::make_unique<vkwave::Buffer>(
    *m_engine->device, "screenshot readback", needed,
    vk::BufferUsageFlagBits::eTransferDst,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
}

// ---------------------------------------------------------------------------
// Per-frame update
// ---------------------------------------------------------------------------

void Scene::update(vkwave::RenderGraph& graph)
{
  data.camera.set_aspect_ratio(
    static_cast<float>(graph.offscreen_group(0).extent().width) /
    static_cast<float>(graph.offscreen_group(0).extent().height));
  pbr_ctx.view_projection = data.camera.view_projection_matrix();
  pbr_ctx.cam_position = data.camera.position();
  pbr_ctx.time = graph.elapsed_time();
}

// ---------------------------------------------------------------------------
// ImGui control panel
// ---------------------------------------------------------------------------

void Scene::draw_ui(Engine& app, double avg_fps)
{
  pipeline->imgui->new_frame();
  ImGui::Begin("vkwave");
  ImGui::Text("%.0f fps", avg_fps);
  ImGui::Separator();

  // Display settings
  if (ImGui::CollapsingHeader("Display"))
  {
    static constexpr struct { vk::PresentModeKHR mode; const char* label; } present_mode_table[] = {
      { vk::PresentModeKHR::eImmediate,    "Immediate (no VSync)" },
      { vk::PresentModeKHR::eMailbox,       "Mailbox (triple buffer)" },
      { vk::PresentModeKHR::eFifo,          "FIFO (VSync)" },
      { vk::PresentModeKHR::eFifoRelaxed,   "FIFO Relaxed" },
    };

    auto current_mode = app.swapchain->present_mode();
    const char* current_label = vk::to_string(current_mode).c_str();
    for (const auto& entry : present_mode_table)
      if (entry.mode == current_mode) { current_label = entry.label; break; }

    if (ImGui::BeginCombo("Present Mode", current_label))
    {
      for (const auto& entry : present_mode_table)
      {
        auto& avail = app.swapchain->available_present_modes();
        if (std::find(avail.begin(), avail.end(), entry.mode) == avail.end())
          continue;

        bool selected = (entry.mode == current_mode);
        if (ImGui::Selectable(entry.label, selected))
        {
          if (entry.mode != current_mode)
          {
            app.graph->drain();
            app.swapchain->set_preferred_present_mode(entry.mode);
            app.swapchain->recreate(app.window.width(), app.window.height());
            app.graph->resize(*app.swapchain);
            resize(*app.swapchain);

            bool fifo = (entry.mode == vk::PresentModeKHR::eFifo
                      || entry.mode == vk::PresentModeKHR::eFifoRelaxed);
            if (fifo)
              app.graph->present_group().set_gating(vkwave::GatingMode::always);
            else
            {
              float refresh = static_cast<float>(app.window.refresh_rate());
              if (refresh > 0.0f)
                app.graph->present_group().set_gating(vkwave::GatingMode::wall_clock, refresh);
            }

            spdlog::info("Present mode changed to {}", vk::to_string(entry.mode));
          }
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    // MSAA toggle
    static constexpr struct { vk::SampleCountFlagBits samples; const char* label; } msaa_table[] = {
      { vk::SampleCountFlagBits::e1, "Off" },
      { vk::SampleCountFlagBits::e2, "2x" },
      { vk::SampleCountFlagBits::e4, "4x" },
      { vk::SampleCountFlagBits::e8, "8x" },
    };

    auto max_samples = app.device->max_usable_sample_count();
    auto current_samples = pipeline->msaa_samples;
    const char* msaa_label = "Off";
    for (const auto& entry : msaa_table)
      if (entry.samples == current_samples) { msaa_label = entry.label; break; }

    if (ImGui::BeginCombo("MSAA", msaa_label))
    {
      for (const auto& entry : msaa_table)
      {
        if (static_cast<uint32_t>(entry.samples) > static_cast<uint32_t>(max_samples))
          continue;

        bool selected = (entry.samples == current_samples);
        if (ImGui::Selectable(entry.label, selected))
        {
          if (entry.samples != current_samples)
            rebuild_pipeline(entry.samples);
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  }
  ImGui::Separator();

  // PBR debug modes
  const char* debug_modes[] = {
    "Final", "Normals", "Base Color", "Metallic",
    "Roughness", "AO", "Emissive", "Clearcoat", "Anisotropy"
  };
  ImGui::Combo("Debug Mode", &pbr_ctx.debug_mode, debug_modes, IM_ARRAYSIZE(debug_modes));

  // Tonemapping
  ImGui::Separator();
  const char* tonemap_modes[] = {
    "None", "Reinhard", "ACES (Fast)", "ACES (Hill)",
    "ACES + Boost", "Khronos PBR Neutral"
  };
  ImGui::Combo("Tonemap", &composite_pass.tonemap_mode, tonemap_modes, IM_ARRAYSIZE(tonemap_modes));
  ImGui::SliderFloat("Exposure", &composite_pass.exposure, 0.1f, 5.0f);

  // IBL environment
  if (!app.config.hdr_paths.empty())
  {
    ImGui::Separator();
    ImGui::Text("Environment");
    auto hdr_label = (data.current_hdr_index >= 0
          && data.current_hdr_index < static_cast<int>(app.config.hdr_paths.size()))
        ? std::filesystem::path(app.config.hdr_paths[data.current_hdr_index]).stem().string()
        : std::string("neutral");
    if (ImGui::BeginCombo("HDR", hdr_label.c_str()))
    {
      for (int i = 0; i < static_cast<int>(app.config.hdr_paths.size()); ++i)
      {
        auto label = std::filesystem::path(app.config.hdr_paths[i]).stem().string();
        bool selected = (i == data.current_hdr_index);
        if (ImGui::Selectable(label.c_str(), selected))
        {
          if (i != data.current_hdr_index)
          {
            switch_ibl(app.config.hdr_paths[i]);
            data.current_hdr_index = i;
          }
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    // RenderDoc: arm a one-shot capture for the next IBL switch. The IBL
    // compute submissions live outside any swapchain present, so the
    // hotkey-based capture flow can't see them.
    if (vkwave::RenderDoc::is_attached())
    {
      ImGui::Checkbox("Capture next IBL (RenderDoc)", &capture_next_ibl_reload);
    }
    else
    {
      ImGui::BeginDisabled();
      bool dummy = false;
      ImGui::Checkbox("Capture next IBL (RenderDoc not attached)", &dummy);
      ImGui::EndDisabled();
    }
  }

  // Model selection
  if (!app.config.model_paths.empty())
  {
    ImGui::Separator();
    ImGui::Text("Model");
    auto model_label = (data.current_model_index >= 0
          && data.current_model_index < static_cast<int>(app.config.model_paths.size()))
        ? std::filesystem::path(app.config.model_paths[data.current_model_index]).stem().string()
        : std::string("cube");
    if (ImGui::BeginCombo("Model", model_label.c_str()))
    {
      for (int i = 0; i < static_cast<int>(app.config.model_paths.size()); ++i)
      {
        auto label = std::filesystem::path(app.config.model_paths[i]).stem().string();
        bool selected = (i == data.current_model_index);
        if (ImGui::Selectable(label.c_str(), selected))
        {
          if (i != data.current_model_index)
          {
            switch_model(app.config.model_paths[i]);
            data.current_model_index = i;
          }
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  }

  // Light controls
  ImGui::Separator();
  ImGui::Text("Directional Light");
  ImGui::SliderFloat3("Direction", &pbr_ctx.light_direction.x, -1.0f, 1.0f);
  ImGui::SliderFloat("Intensity", &pbr_ctx.light_intensity, 0.0f, 10.0f);
  ImGui::ColorEdit3("Light Color", &pbr_ctx.light_color.x);

  // Feature toggles
  ImGui::Separator();
  ImGui::Text("Features");
  ImGui::Checkbox("Normal Mapping", &pbr_ctx.enable_normal_mapping);
  ImGui::Checkbox("Emissive", &pbr_ctx.enable_emissive);
  ImGui::Checkbox("Clear Coat", &pbr_ctx.enable_clearcoat);

  // Mipmapping A/B: off forces mip 0 via a large negative LOD bias.
  static bool mipmaps_on = true;
  if (ImGui::Checkbox("Mipmaps", &mipmaps_on))
    pbr_ctx.mip_bias = mipmaps_on ? 0.0f : -16.0f;

  // Global clear-coat override (non-glTF): force a coat onto every material so
  // the effect can be previewed on assets that don't author KHR_materials_clearcoat.
  static bool cc_override = false;
  static float cc_strength = 1.0f;
  if (ImGui::Checkbox("Override Clear Coat", &cc_override))
    pbr_ctx.clearcoat_override = cc_override ? cc_strength : -1.0f;
  if (cc_override)
  {
    if (ImGui::SliderFloat("Coat Strength", &cc_strength, 0.0f, 1.0f))
      pbr_ctx.clearcoat_override = cc_strength;
    ImGui::SliderFloat("Coat Roughness", &pbr_ctx.clearcoat_roughness_override, 0.0f, 1.0f);
  }

  ImGui::Checkbox("Anisotropy", &pbr_ctx.enable_anisotropy);

  // Global anisotropy override (non-glTF): force an elongated highlight onto
  // every material to preview the effect on assets without the extension.
  static bool ani_override = false;
  static float ani_strength = 1.0f;
  if (ImGui::Checkbox("Override Anisotropy", &ani_override))
    pbr_ctx.anisotropy_override = ani_override ? ani_strength : -1.0f;
  if (ani_override)
  {
    if (ImGui::SliderFloat("Aniso Strength", &ani_strength, 0.0f, 1.0f))
      pbr_ctx.anisotropy_override = ani_strength;
    ImGui::SliderFloat("Aniso Rotation", &pbr_ctx.anisotropy_rotation_override, 0.0f, 6.2831853f);
  }

  // Material overrides (preview): force metallic/roughness onto every material,
  // e.g. to tweak the single-material cube. Off by default (materials use their
  // authored SSBO values).
  ImGui::Separator();
  ImGui::Text("Material Overrides");

  static bool met_override = false;
  static float met_value = 1.0f;
  if (ImGui::Checkbox("Override Metallic", &met_override))
    pbr_ctx.metallic_override = met_override ? met_value : -1.0f;
  if (met_override && ImGui::SliderFloat("Metallic", &met_value, 0.0f, 1.0f))
    pbr_ctx.metallic_override = met_value;

  static bool rough_override = false;
  static float rough_value = 1.0f;
  if (ImGui::Checkbox("Override Roughness", &rough_override))
    pbr_ctx.roughness_override = rough_override ? rough_value : -1.0f;
  if (rough_override && ImGui::SliderFloat("Roughness", &rough_value, 0.0f, 1.0f))
    pbr_ctx.roughness_override = rough_value;

  ImGui::Separator();
  if (screenshot_requested || screenshot_in_flight || screenshot_compressing)
  {
    ImGui::BeginDisabled();
    ImGui::Button("Screenshot (saving...)");
    ImGui::EndDisabled();
  }
  else if (ImGui::Button("Screenshot"))
  {
    screenshot_requested = true;
  }

  ImGui::End();
}
