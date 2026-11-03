// bbport (macOS, M5 A5): see vk_null_resources.h.
#ifdef __APPLE__
#include "common/assert.h"
#include "common/logging/log.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_null_resources.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {
namespace {
constexpr std::array ClassFormats = {vk::Format::eR8G8B8A8Unorm, vk::Format::eR32Sint,
                                     vk::Format::eR32Uint};
struct Kind {
    vk::ImageType image_type;
    vk::ImageViewType view_type;
    u32 layers;
    vk::SampleCountFlagBits samples;
};
constexpr std::array<Kind, 7> Kinds = {{
    {vk::ImageType::e1D, vk::ImageViewType::e1D, 1, vk::SampleCountFlagBits::e1},
    {vk::ImageType::e1D, vk::ImageViewType::e1DArray, 1, vk::SampleCountFlagBits::e1},
    {vk::ImageType::e2D, vk::ImageViewType::e2D, 1, vk::SampleCountFlagBits::e1},
    {vk::ImageType::e2D, vk::ImageViewType::e2DArray, 6, vk::SampleCountFlagBits::e1},
    {vk::ImageType::e3D, vk::ImageViewType::e3D, 1, vk::SampleCountFlagBits::e1},
    {vk::ImageType::e2D, vk::ImageViewType::e2D, 1, vk::SampleCountFlagBits::e4},
    {vk::ImageType::e2D, vk::ImageViewType::e2DArray, 1, vk::SampleCountFlagBits::e4},
}};
int KindOf(AmdGpu::ImageType type) {
    switch (type) {
    case AmdGpu::ImageType::Color1D:
        return 0;
    case AmdGpu::ImageType::Color1DArray:
        return 1;
    case AmdGpu::ImageType::Color2D:
        return 2;
    case AmdGpu::ImageType::Color2DArray:
    case AmdGpu::ImageType::Cube: // declared as a 2D array (ConvertCubeTo2DArray)
        return 3;
    case AmdGpu::ImageType::Color3D:
        return 4;
    case AmdGpu::ImageType::Color2DMsaa:
        return 5;
    case AmdGpu::ImageType::Color2DMsaaArray:
        return 6;
    default:
        return 2;
    }
}
} // namespace

void NullResources::CreateImage(Image& out, vk::ImageType image_type, vk::ImageViewType view_type,
                                vk::Format format, u32 layers, vk::SampleCountFlagBits samples,
                                bool depth) {
    const bool msaa = samples != vk::SampleCountFlagBits::e1;
    vk::ImageUsageFlags usage =
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
    if (depth) {
        usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    } else {
        if (!msaa) {
            usage |= vk::ImageUsageFlagBits::eStorage; // Metal: no MSAA storage textures
        } else {
            usage |= vk::ImageUsageFlagBits::eColorAttachment;
        }
    }
    const VkImageCreateInfo image_ci = static_cast<VkImageCreateInfo>(vk::ImageCreateInfo{
        .imageType = image_type,
        .format = format,
        .extent = {1, 1, 1},
        .mipLevels = 1,
        .arrayLayers = layers,
        .samples = samples,
        .usage = usage,
    });
    const VmaAllocationCreateInfo alloc_ci = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    VkImage image{};
    if (vmaCreateImage(instance.GetAllocator(), &image_ci, &alloc_ci, &image, &out.allocation,
                       nullptr) != VK_SUCCESS) {
        LOG_WARNING(Render_Vulkan, "Null resources: cannot create a {} {} dummy image",
                    vk::to_string(view_type), vk::to_string(format));
        return;
    }
    out.image = vk::Image{image};
    const vk::ImageAspectFlags aspect =
        depth ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
    out.view = Check<"create null image view">(instance.GetDevice().createImageView({
        .image = out.image,
        .viewType = view_type,
        .format = format,
        .subresourceRange = {aspect, 0, 1, 0, layers},
    }));
}

NullResources::NullResources(const Instance& instance_, Scheduler& scheduler)
    : instance{instance_} {
    const VkBufferCreateInfo buffer_ci = static_cast<VkBufferCreateInfo>(vk::BufferCreateInfo{
        .size = BufferSize,
        .usage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer |
                 vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
                 vk::BufferUsageFlagBits::eTransferDst,
    });
    const VmaAllocationCreateInfo alloc_ci = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    VkBuffer raw_buffer{};
    ASSERT_MSG(vmaCreateBuffer(instance.GetAllocator(), &buffer_ci, &alloc_ci, &raw_buffer,
                               &buffer_allocation, nullptr) == VK_SUCCESS,
               "Null resources: cannot create the dummy buffer");
    buffer = vk::Buffer{raw_buffer};
    for (u32 k = 0; k < NumKinds; ++k) {
        for (u32 c = 0; c < ClassFormats.size(); ++c) {
            CreateImage(images[k][c], Kinds[k].image_type, Kinds[k].view_type, ClassFormats[c],
                        Kinds[k].layers, Kinds[k].samples, false);
        }
    }
    CreateImage(depth_images[0], vk::ImageType::e2D, vk::ImageViewType::e2D,
                vk::Format::eD32Sfloat, 1, vk::SampleCountFlagBits::e1, true);
    CreateImage(depth_images[1], vk::ImageType::e2D, vk::ImageViewType::e2DArray,
                vk::Format::eD32Sfloat, 6, vk::SampleCountFlagBits::e1, true);

    // Zero everything once and leave the images in General (the layout the descriptors use).
    scheduler.Record([this](vk::CommandBuffer cmdbuf) {
        cmdbuf.fillBuffer(buffer, 0, BufferSize, 0);
        std::vector<vk::ImageMemoryBarrier2> to_dst, to_general;
        const auto add = [&](const Image& image, u32 layers, bool depth) {
            if (!image.image) {
                return;
            }
            const vk::ImageSubresourceRange range{
                depth ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor, 0, 1,
                0, layers};
            to_dst.push_back({.srcStageMask = vk::PipelineStageFlagBits2::eNone,
                              .dstStageMask = vk::PipelineStageFlagBits2::eAllTransfer,
                              .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
                              .oldLayout = vk::ImageLayout::eUndefined,
                              .newLayout = vk::ImageLayout::eTransferDstOptimal,
                              .image = image.image,
                              .subresourceRange = range});
            to_general.push_back({.srcStageMask = vk::PipelineStageFlagBits2::eAllTransfer,
                                  .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                  .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                                  .dstAccessMask = vk::AccessFlagBits2::eMemoryRead |
                                                   vk::AccessFlagBits2::eMemoryWrite,
                                  .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                                  .newLayout = vk::ImageLayout::eGeneral,
                                  .image = image.image,
                                  .subresourceRange = range});
        };
        for (u32 k = 0; k < NumKinds; ++k) {
            for (const Image& image : images[k]) {
                add(image, Kinds[k].layers, false);
            }
        }
        add(depth_images[0], 1, true);
        add(depth_images[1], 6, true);
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = u32(to_dst.size()),
                                 .pImageMemoryBarriers = to_dst.data()});
        for (u32 k = 0; k < NumKinds; ++k) {
            for (const Image& image : images[k]) {
                if (image.image) {
                    cmdbuf.clearColorImage(image.image, vk::ImageLayout::eTransferDstOptimal,
                                           vk::ClearColorValue{},
                                           vk::ImageSubresourceRange{
                                               vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                               Kinds[k].layers});
                }
            }
        }
        for (u32 d = 0; d < depth_images.size(); ++d) {
            if (depth_images[d].image) {
                cmdbuf.clearDepthStencilImage(
                    depth_images[d].image, vk::ImageLayout::eTransferDstOptimal,
                    vk::ClearDepthStencilValue{0.f, 0},
                    vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth, 0, 1, 0,
                                              d ? 6u : 1u});
            }
        }
        const vk::MemoryBarrier2 buffer_barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        };
        cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1,
                                 .pMemoryBarriers = &buffer_barrier,
                                 .imageMemoryBarrierCount = u32(to_general.size()),
                                 .pImageMemoryBarriers = to_general.data()});
    });
    LOG_WARNING(Render_Vulkan,
                "nullDescriptor unavailable: null buffer/image descriptors use zero-filled "
                "dummies ({} KiB buffer, 1x1 images)",
                BufferSize / 1024);
}

NullResources::~NullResources() {
    const vk::Device device = instance.GetDevice();
    const auto destroy = [&](Image& image) {
        if (image.view) {
            device.destroyImageView(image.view);
        }
        if (image.image) {
            vmaDestroyImage(instance.GetAllocator(), image.image, image.allocation);
        }
    };
    for (auto& kind : images) {
        for (Image& image : kind) {
            destroy(image);
        }
    }
    for (Image& image : depth_images) {
        destroy(image);
    }
    vmaDestroyBuffer(instance.GetAllocator(), buffer, buffer_allocation);
}

vk::ImageView NullResources::ImageView(AmdGpu::ImageType type, AmdGpu::NumberClass number_class,
                                       bool is_depth, bool storage) const noexcept {
    const int kind = KindOf(type);
    if (is_depth && !storage && (kind == 2 || kind == 3)) {
        return depth_images[kind == 2 ? 0 : 1].view;
    }
    if (storage && Kinds[kind].samples != vk::SampleCountFlagBits::e1) {
        return {};
    }
    return images[kind][static_cast<u32>(number_class)].view;
}

} // namespace Vulkan
#endif
