#pragma once

/**
 * @file screenshot.h
 * @brief Non-blocking screenshot capture from the offscreen HDR image.
 *
 * @par Design overview
 *
 * Screenshots are captured from the offscreen R16G16B16A16_SFLOAT image that
 * the PBR pass writes to.  This image is already ring-buffered, single-sample
 * (MSAA resolves inside the render pass), and owned by us — not the swapchain.
 *
 * @par Why this is fast
 *
 * Six properties keep the screenshot path out of the rendering pipeline:
 *
 * 1. **No device or queue idle.**  We never call @c vkDeviceWaitIdle() or
 *    @c vkQueueWaitIdle().  The GPU keeps working on future frames while the
 *    copy completes in the background.
 *
 * 2. **Copy is ordered naturally within the same submit.**  The copy command
 *    is recorded after @c endRenderPass() in the PBR group's @c post_record_fn,
 *    inside the same command buffer.  Execution order is already correct; the
 *    pipeline barrier only ensures memory visibility and layout — not a global
 *    stall.
 *
 * 3. **Only the screenshot slot is serialized, not frames.**  At most one
 *    screenshot job is in flight at a time.  That serialization applies only
 *    to screenshot requests, not to rendering.  Frames continue to pipeline
 *    normally.
 *
 * 4. **Fence polling is non-blocking.**  @c vkGetFenceStatus() (~ns) checks
 *    whether the GPU finished the specific copy.  Rendering of newer frames
 *    continues regardless.
 *
 * 5. **Per-frame resources are untouched.**  The existing N-frame ring (UBOs,
 *    command buffers, descriptors) is independent of the screenshot readback
 *    buffer.
 *
 * 6. **No interference with presentation pacing.**  We capture from our own
 *    offscreen image, so we never get entangled with swapchain ownership or
 *    vsync timing.  ImGui is always visible in the captured frame.
 *
 * @par Pipeline
 *
 * @code
 *   UI button  ──►  ensure readback buffer (grow-only, main thread)
 *                          │
 *              ┌───────────▼───────────────────────────────────────┐
 *   PBR submit │  ...render pass...  endRenderPass()              │
 *              │  barrier: ShaderReadOnly → TransferSrc            │
 *              │  vkCmdCopyImageToBuffer(HDR → HOST_VISIBLE)      │
 *              │  barrier: TransferSrc → ShaderReadOnly            │
 *              │                              set_next_fence(fence)│
 *              └──────────────────────────────────────────────────-┘
 *                          │
 *              ┌───────────▼──────────────────────────┐
 *   main loop  │  vkGetFenceStatus()  (non-blocking)  │
 *              └───────────┬──────────────────────────┘
 *                          │  signaled
 *              ┌───────────▼──────────────────────────┐
 *   worker     │  map, half→float, Reinhard tonemap,  │
 *   thread     │  gamma, PNG compress, unmap          │
 *              └───────────┬──────────────────────────┘
 *                          │
 *              ┌───────────▼──────────────────────────┐
 *   main thread│  fwrite PNG to disk                  │
 *              └─────────────────────────────────────-┘
 * @endcode
 */

#include <vkwave/core/buffer.h>

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <string>
#include <vector>

/// Record barrier + copy from an offscreen HDR image to a HOST_VISIBLE buffer.
/// The image must be in eShaderReadOnlyOptimal layout (post-render-pass).
/// After the copy, the image is transitioned back to eShaderReadOnlyOptimal
/// so the composite pass can sample it normally.
void record_hdr_screenshot_copy(vk::CommandBuffer cmd,
                                vk::Image hdr_image,
                                vk::Extent2D extent,
                                vk::Buffer readback_buf);

/// Map HOST_VISIBLE buffer, convert HDR float16 to LDR uint8 with tonemap,
/// compress to PNG in memory, unmap.
/// CPU-heavy (tonemap + zlib) — safe to call from a background thread.
/// Returns the PNG file contents as a byte vector.
std::vector<uint8_t> compress_screenshot(vkwave::Buffer& readback,
                                         vk::Format format,
                                         vk::Extent2D extent);

/// Write pre-compressed PNG data to disk. Call from the main thread.
void write_screenshot(const std::vector<uint8_t>& png_data,
                      const std::string& filename);
