// bbport (macOS, M5 A5): stand-ins for null descriptors on drivers without
// VK_EXT_robustness2 nullDescriptor (MoltenVK). The renderer writes VK_NULL_HANDLE for buffers and
// images a shader declares but the game left unbound (zero address / size, invalid T#); where the
// feature is missing those writes get a zero-filled dummy instead: reads return 0, as a null
// descriptor would, writes land in the dummy. Created only when Instance::IsNullDescriptorSupported()
// is false; with the feature the renderer keeps writing null descriptors.
#pragma once
#ifdef __APPLE__
#include <array>
#include <vk_mem_alloc.h>
#include "video_core/amdgpu/pixel_format.h"
#include "video_core/amdgpu/resource.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class Instance;
class Scheduler;

class NullResources {
public:
    static constexpr vk::DeviceSize BufferSize = 64 * 1024;

    NullResources(const Instance& instance, Scheduler& scheduler);
    ~NullResources();
    NullResources(const NullResources&) = delete;
    NullResources& operator=(const NullResources&) = delete;

    /// Zero-filled buffer for uniform/storage/vertex bindings.
    vk::Buffer Buffer() const noexcept {
        return buffer;
    }
    /// Zero-filled 1x1 image view (layout General) matching the type the shader declares for the
    /// T# (`GetViewType(is_array)` and the number class); null when none fits (MSAA storage).
    vk::ImageView ImageView(AmdGpu::ImageType type, AmdGpu::NumberClass number_class,
                            bool is_depth, bool storage) const noexcept;

private:
    struct Image {
        vk::Image image{};
        VmaAllocation allocation{};
        vk::ImageView view{};
    };
    void CreateImage(Image& out, vk::ImageType image_type, vk::ImageViewType view_type,
                     vk::Format format, u32 layers, vk::SampleCountFlagBits samples,
                     bool depth);

    const Instance& instance;
    vk::Buffer buffer{};
    VmaAllocation buffer_allocation{};
    // [view kind][class]: kinds 1D, 1DArray, 2D, 2DArray, 3D, 2DMsaa, 2DMsaaArray; classes
    // Float, Sint, Uint. Depth: 2D, 2DArray.
    static constexpr u32 NumKinds = 7;
    std::array<std::array<Image, 3>, NumKinds> images{};
    std::array<Image, 2> depth_images{};
};

} // namespace Vulkan
#endif
