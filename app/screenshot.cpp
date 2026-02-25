#include "screenshot.h"

#include <spdlog/spdlog.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Half-float → float conversion (IEEE 754 binary16)
// ---------------------------------------------------------------------------

static float half_to_float(uint16_t h)
{
  uint32_t sign = (h >> 15) & 0x1;
  uint32_t exp  = (h >> 10) & 0x1f;
  uint32_t mant = h & 0x3ff;

  uint32_t f;
  if (exp == 0)
  {
    if (mant == 0)
      f = sign << 31; // ±0
    else
    {
      // Denormalized: convert to normalized float
      exp = 1;
      while (!(mant & 0x400)) { mant <<= 1; exp--; }
      mant &= 0x3ff;
      f = (sign << 31) | ((exp + 127 - 15) << 23) | (mant << 13);
    }
  }
  else if (exp == 31)
    f = (sign << 31) | 0x7f800000 | (mant << 13); // Inf/NaN
  else
    f = (sign << 31) | ((exp + 127 - 15) << 23) | (mant << 13); // Normalized

  float result;
  std::memcpy(&result, &f, sizeof(float));
  return result;
}

// ---------------------------------------------------------------------------
// GPU copy: HDR image → HOST_VISIBLE buffer
// ---------------------------------------------------------------------------

void record_hdr_screenshot_copy(vk::CommandBuffer cmd,
                                vk::Image hdr_image,
                                vk::Extent2D extent,
                                vk::Buffer readback_buf)
{
  const uint32_t w = extent.width;
  const uint32_t h = extent.height;
  const vk::DeviceSize byte_size = static_cast<vk::DeviceSize>(w) * h * 8; // RGBA16F = 8 bytes/pixel

  // Barrier: eShaderReadOnlyOptimal → eTransferSrcOptimal
  vk::ImageMemoryBarrier to_src{};
  to_src.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
  to_src.dstAccessMask = vk::AccessFlagBits::eTransferRead;
  to_src.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  to_src.newLayout = vk::ImageLayout::eTransferSrcOptimal;
  to_src.image = hdr_image;
  to_src.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

  cmd.pipelineBarrier(
    vk::PipelineStageFlagBits::eColorAttachmentOutput,
    vk::PipelineStageFlagBits::eTransfer,
    {}, {}, {}, to_src);

  // Copy image → buffer
  vk::BufferImageCopy region{};
  region.imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
  region.imageExtent = vk::Extent3D{ w, h, 1 };

  cmd.copyImageToBuffer(hdr_image, vk::ImageLayout::eTransferSrcOptimal,
    readback_buf, region);

  // Barrier: eTransferSrcOptimal → eShaderReadOnlyOptimal
  // (composite pass needs to sample this image)
  vk::ImageMemoryBarrier to_shader{};
  to_shader.srcAccessMask = vk::AccessFlagBits::eTransferRead;
  to_shader.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  to_shader.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
  to_shader.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  to_shader.image = hdr_image;
  to_shader.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

  // Host read barrier on the buffer
  vk::BufferMemoryBarrier host_barrier{};
  host_barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
  host_barrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
  host_barrier.buffer = readback_buf;
  host_barrier.size = byte_size;

  cmd.pipelineBarrier(
    vk::PipelineStageFlagBits::eTransfer,
    vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eHost,
    {}, {}, host_barrier, to_shader);
}

// ---------------------------------------------------------------------------
// stbi PNG callback
// ---------------------------------------------------------------------------

static void png_write_to_vector(void* context, void* data, int size)
{
  auto* vec = static_cast<std::vector<uint8_t>*>(context);
  auto* bytes = static_cast<const uint8_t*>(data);
  vec->insert(vec->end(), bytes, bytes + size);
}

// ---------------------------------------------------------------------------
// HDR → LDR conversion + PNG compression
// ---------------------------------------------------------------------------

std::vector<uint8_t> compress_screenshot(vkwave::Buffer& readback,
                                         vk::Format format,
                                         vk::Extent2D extent)
{
  const uint32_t w = extent.width;
  const uint32_t h = extent.height;

  readback.map();

  std::vector<uint8_t> ldr(w * h * 4);

  if (format == vk::Format::eR16G16B16A16Sfloat)
  {
    // HDR float16 → LDR uint8 with Reinhard tonemap + gamma
    auto* f16 = static_cast<const uint16_t*>(readback.mapped_data());
    for (uint32_t i = 0; i < w * h; ++i)
    {
      float r = half_to_float(f16[i * 4 + 0]);
      float g = half_to_float(f16[i * 4 + 1]);
      float b = half_to_float(f16[i * 4 + 2]);

      // Simple Reinhard tonemap + gamma 2.2
      r = std::pow(r / (1.0f + r), 1.0f / 2.2f);
      g = std::pow(g / (1.0f + g), 1.0f / 2.2f);
      b = std::pow(b / (1.0f + b), 1.0f / 2.2f);

      ldr[i * 4 + 0] = static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
      ldr[i * 4 + 1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
      ldr[i * 4 + 2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
      ldr[i * 4 + 3] = 255;
    }
  }
  else
  {
    // Legacy: uint8 BGRA/RGBA swapchain format
    auto* pixels = static_cast<uint8_t*>(readback.mapped_data());
    const bool bgra = (format == vk::Format::eB8G8R8A8Srgb
                    || format == vk::Format::eB8G8R8A8Unorm);
    std::memcpy(ldr.data(), pixels, w * h * 4);
    if (bgra)
    {
      for (uint32_t i = 0; i < w * h; ++i)
        std::swap(ldr[i * 4 + 0], ldr[i * 4 + 2]);
    }
  }

  readback.unmap();

  std::vector<uint8_t> png_data;
  png_data.reserve(w * h);
  stbi_write_png_to_func(png_write_to_vector, &png_data,
    static_cast<int>(w), static_cast<int>(h),
    4, ldr.data(), static_cast<int>(w * 4));

  return png_data;
}

void write_screenshot(const std::vector<uint8_t>& png_data,
                      const std::string& filename)
{
  FILE* f = fopen(filename.c_str(), "wb");
  if (f)
  {
    fwrite(png_data.data(), 1, png_data.size(), f);
    fclose(f);
    spdlog::info("Screenshot saved: {} ({} bytes)", filename, png_data.size());
  }
  else
  {
    spdlog::error("Failed to write screenshot: {}", filename);
  }
}
