#pragma once
#include <cstdint>
#include <openxr/openxr.h>
#include <string>
#include <vector>

// The compositor owns its Vulkan resources. Guest images are copied while
// acquired, before release, so frame translation never accesses a released image.
void xr_layers_instance(PFN_xrGetInstanceProcAddr get, XrInstance instance,
                        const std::vector<const char *> &extensions);
void xr_layers_note(const std::string &name, const uint64_t *args, XrResult result);
XrResult xr_layers_before_release(XrSwapchain swapchain);
void xr_layers_destroy(const std::string &name, uint64_t handle);
XrResult xr_layers_end(XrSession session, const XrFrameEndInfo *frame);
