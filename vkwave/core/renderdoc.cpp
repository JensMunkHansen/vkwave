#include <vkwave/core/renderdoc.h>

#include <spdlog/spdlog.h>

#if defined(VKWAVE_HAVE_RENDERDOC)
#  include <renderdoc_app.h>
#  if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#  else
#    include <dlfcn.h>
#  endif
#endif

namespace vkwave
{

#if defined(VKWAVE_HAVE_RENDERDOC)

namespace
{
RENDERDOC_API_1_5_0* g_rdoc = nullptr;
bool g_initialized = false;
void* g_device_ptr = nullptr;  // dispatch-table pointer from VkInstance

pRENDERDOC_GetAPI resolve_get_api()
{
#  if defined(_WIN32)
  HMODULE mod = GetModuleHandleA("renderdoc.dll");
  if (!mod)
    return nullptr;
  return reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(mod, "RENDERDOC_GetAPI"));
#  else
  // RTLD_NOLOAD: only succeed if librenderdoc is already mapped.
  void* mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
  if (!mod)
    return nullptr;
  return reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(mod, "RENDERDOC_GetAPI"));
#  endif
}
} // namespace

bool RenderDoc::initialize()
{
  if (g_rdoc)
    return true;

  auto get_api = resolve_get_api();
  if (!get_api)
  {
    // On Linux the capture library is loaded by the Vulkan loader during
    // vkCreateInstance (via VK_LAYER_RENDERDOC_Capture), not at process start.
    // Don't latch the failure — allow a retry once the instance exists.
    if (!g_initialized)
      spdlog::debug("RenderDoc: capture library not loaded yet — will retry after instance creation");
    g_initialized = true;
    return false;
  }

  void* api_ptr = nullptr;
  if (get_api(eRENDERDOC_API_Version_1_5_0, &api_ptr) != 1 || !api_ptr)
  {
    spdlog::warn("RenderDoc: RENDERDOC_GetAPI failed (version mismatch?)");
    g_initialized = true;
    return false;
  }

  g_rdoc = static_cast<RENDERDOC_API_1_5_0*>(api_ptr);
  g_initialized = true;
  int major = 0, minor = 0, patch = 0;
  g_rdoc->GetAPIVersion(&major, &minor, &patch);
  spdlog::info("RenderDoc: attached (API {}.{}.{})", major, minor, patch);
  return true;
}

bool RenderDoc::is_attached()
{
  return g_rdoc != nullptr;
}

void RenderDoc::set_vulkan_instance(vk::Instance instance)
{
  if (!instance)
  {
    g_device_ptr = nullptr;
    return;
  }
  // On Linux the RenderDoc library is loaded by the Vulkan layer mechanism
  // during vkCreateInstance, so the API may not be resolvable until now.
  if (!g_rdoc)
    initialize();
  // RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(inst) == *(void**)inst
  VkInstance raw = static_cast<VkInstance>(instance);
  g_device_ptr = RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(raw);
  if (g_rdoc)
    spdlog::debug("RenderDoc: bound to Vulkan instance dispatch pointer {}", g_device_ptr);
}

void RenderDoc::begin_capture()
{
  if (g_rdoc)
    g_rdoc->StartFrameCapture(g_device_ptr, nullptr);
}

void RenderDoc::end_capture()
{
  if (!g_rdoc)
    return;
  uint32_t ok = g_rdoc->EndFrameCapture(g_device_ptr, nullptr);
  if (!ok)
    spdlog::warn("RenderDoc: EndFrameCapture returned 0 — capture may be empty");
}

#else // VKWAVE_HAVE_RENDERDOC

bool RenderDoc::initialize() { return false; }
bool RenderDoc::is_attached() { return false; }
void RenderDoc::set_vulkan_instance(vk::Instance) {}
void RenderDoc::begin_capture() {}
void RenderDoc::end_capture() {}

#endif

} // namespace vkwave
