// SPDX-License-Identifier: MIT
#pragma once
// Include the project's OpenXR types before this header.
inline PFN_xrVoidFunction resolve_legacy_function(
    const char* name, PFN_xrVoidFunction exported, PFN_xrGetInstanceProcAddr lookup) {
    if (!name) return nullptr;
    if (exported) return exported;
    PFN_xrVoidFunction function = nullptr;
    if (lookup && lookup(nullptr, name, &function) == XR_SUCCESS) return function;
    return nullptr;
}
