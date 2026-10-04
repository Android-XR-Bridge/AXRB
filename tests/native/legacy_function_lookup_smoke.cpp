// SPDX-License-Identifier: MIT
#include "../../runtime/src/openxr_dispatch/openxr_minimal.h"
#include "../../runtime/apk/systemdriver/legacy_function_lookup.h"
#include <cassert>
#include <cstring>
static unsigned calls = 0;
static void XRAPI_CALL initialized() {}
static void XRAPI_CALL exported() {}
static XrResult XRAPI_CALL lookup(XrInstance instance, const char* name, PFN_xrVoidFunction* result) {
    assert(!instance); ++calls;
    *result = initialized;
    return std::strcmp(name, "xrInitializeLoaderKHR") ? XR_ERROR_FUNCTION_UNSUPPORTED : XR_SUCCESS;
}
int main() {
    assert(resolve_legacy_function("xrInitializeLoaderKHR", nullptr, lookup) == initialized);
    assert(calls == 1);
    assert(resolve_legacy_function("xrInitializeLoaderKHR", exported, lookup) == exported);
    assert(calls == 1);
    assert(!resolve_legacy_function("missing", nullptr, lookup));
    assert(!resolve_legacy_function("missing", nullptr, nullptr));
    assert(!resolve_legacy_function(nullptr, exported, lookup));
}
