#include "transmission.h"

#include <array>

void record_transmission_snapshot_copy(vk::CommandBuffer cmd,
                                       vk::Image hdr_image,
                                       vk::Image snapshot_image,
                                       vk::Extent2D extent)
{
  const vk::ImageSubresourceRange color1{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

  // --- Pre-copy transitions -------------------------------------------------
  // HDR: render-pass final layout (ShaderReadOnly) -> TransferSrc. Wait on the
  // color writes that produced it.
  vk::ImageMemoryBarrier hdr_to_src{};
  hdr_to_src.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
  hdr_to_src.dstAccessMask = vk::AccessFlagBits::eTransferRead;
  hdr_to_src.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  hdr_to_src.newLayout = vk::ImageLayout::eTransferSrcOptimal;
  hdr_to_src.image = hdr_image;
  hdr_to_src.subresourceRange = color1;

  // Snapshot: contents are fully overwritten, so discard the old layout
  // (Undefined) rather than preserve it — cheaper, and correct since the copy
  // writes every texel.
  vk::ImageMemoryBarrier snap_to_dst{};
  snap_to_dst.srcAccessMask = {};
  snap_to_dst.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
  snap_to_dst.oldLayout = vk::ImageLayout::eUndefined;
  snap_to_dst.newLayout = vk::ImageLayout::eTransferDstOptimal;
  snap_to_dst.image = snapshot_image;
  snap_to_dst.subresourceRange = color1;

  const std::array<vk::ImageMemoryBarrier, 2> pre{ hdr_to_src, snap_to_dst };
  cmd.pipelineBarrier(
    vk::PipelineStageFlagBits::eColorAttachmentOutput,
    vk::PipelineStageFlagBits::eTransfer,
    {}, {}, {}, pre);

  // --- Copy -----------------------------------------------------------------
  vk::ImageCopy region{};
  region.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
  region.dstSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
  region.extent = vk::Extent3D{ extent.width, extent.height, 1 };
  cmd.copyImage(hdr_image, vk::ImageLayout::eTransferSrcOptimal,
                snapshot_image, vk::ImageLayout::eTransferDstOptimal, region);

  // --- Post-copy transitions (TRANSFER -> FRAGMENT_SHADER) ------------------
  // HDR back to ShaderReadOnly so the composite pass can sample it.
  vk::ImageMemoryBarrier hdr_to_ro{};
  hdr_to_ro.srcAccessMask = vk::AccessFlagBits::eTransferRead;
  hdr_to_ro.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  hdr_to_ro.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
  hdr_to_ro.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  hdr_to_ro.image = hdr_image;
  hdr_to_ro.subresourceRange = color1;

  // Snapshot to ShaderReadOnly so the refraction pass can sample it.
  vk::ImageMemoryBarrier snap_to_ro{};
  snap_to_ro.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
  snap_to_ro.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  snap_to_ro.oldLayout = vk::ImageLayout::eTransferDstOptimal;
  snap_to_ro.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  snap_to_ro.image = snapshot_image;
  snap_to_ro.subresourceRange = color1;

  const std::array<vk::ImageMemoryBarrier, 2> post{ hdr_to_ro, snap_to_ro };
  cmd.pipelineBarrier(
    vk::PipelineStageFlagBits::eTransfer,
    vk::PipelineStageFlagBits::eFragmentShader,
    {}, {}, {}, post);
}
