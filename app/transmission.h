#pragma once

#include <vulkan/vulkan.hpp>

/// Transmission (glass) helpers — the consumer of the rendering foundations.
/// See docs/transmission.md.

/// Copy the just-rendered opaque HDR into the per-slot transmission snapshot, so
/// the refraction pass can sample the scene *behind* glass (you cannot read the
/// render target you are writing). Recorded at the tail of the opaque command
/// buffer — after its render pass ends, same submission — so it adds no extra
/// vkQueueSubmit.
///
/// Both images are the same format, extent and sample count, so this is an exact
/// vkCmdCopyImage (no scaling). Barriers are fine-grained on exactly the two
/// images involved (never a global ALL_COMMANDS barrier), per the overlap design:
///   - HDR:      ShaderReadOnly -> TransferSrc -> ShaderReadOnly (composite still samples it)
///   - snapshot: Undefined      -> TransferDst -> ShaderReadOnly (refraction pass samples it)
/// The closing snapshot TRANSFER -> FRAGMENT_SHADER transition is the dependency
/// the (future) transmission draw pass relies on.
void record_transmission_snapshot_copy(vk::CommandBuffer cmd,
                                       vk::Image hdr_image,
                                       vk::Image snapshot_image,
                                       vk::Extent2D extent);
