#pragma once

namespace axrb {

// Registers the host functions behind the guest's libvulkan.so ("vk.*").
void vulkan_init();

// Activates AXRB's GPU-share layer (VK_LAYER_AXRB_gpu_share) for the host
// instances, from AXRB_GPU_LAYER_DIR or gpu\ beside the executable, unless
// AXRB_GPU_SHARE=0. Must run before the first Vulkan call. True if active.
bool vulkan_prepare_gpu_share();

}  // namespace axrb
