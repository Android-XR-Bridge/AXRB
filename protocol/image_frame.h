#pragma once

#include <cstdint>
#include <cmath>
#include <initializer_list>
#include "pose_frame.h"
#include "layer_color.h"

namespace axrb::protocol {

// Legacy fallback when no host view recommendation is available.
constexpr uint32_t kTransportEyeDimension = 1024;

constexpr uint32_t kImageFrameMagic = 0x49585241; // AXRI, little-endian.
constexpr uint16_t kImageFrameVersion = 1;
constexpr uint16_t kEmptyImageFrameVersion = 12;
constexpr uint16_t kProjectionImageFrameVersion = 2;
constexpr uint16_t kQuadImageFrameVersion = 4;
constexpr uint16_t kQuadGpuFrameVersion = 5;
// Count/index fields in legacy mixed packets are 16 bits; not a storage allocation size.
constexpr uint32_t kMaxWireCompositionLayers = 0xffff;
constexpr uint16_t kMixedProjectionGpuFrameVersion = 6;
constexpr uint16_t kMixedQuadGpuFrameVersion = 7;
constexpr uint16_t kEquirectGpuFrameVersion = 8;
constexpr uint16_t kMixedEquirectGpuFrameVersion = 9;
constexpr uint16_t kEquirectImageFrameVersion = 10;
inline bool equirect_gpu_version(uint16_t v) { return v == 8 || v == 9; }
inline bool equirect_version(uint16_t v) { return equirect_gpu_version(v) || v == kEquirectImageFrameVersion; }
inline bool mixed_gpu_version(uint16_t version) { return version == 6 || version == 7 || version == 9; }
inline bool valid_mixed_part(uint16_t version, uint32_t part) {
    const uint32_t count = part >> 16, index = part & 0xffff;
    return count >= 2 && count <= kMaxWireCompositionLayers && index < count &&
        mixed_gpu_version(version);
}
constexpr uint16_t kImageFrameTypeRgba8 = 2;
constexpr uint32_t kImageFrameFormatRgba8 = 1;

struct ImageFrameHeader {
    uint32_t magic = kImageFrameMagic;
    uint16_t version = kImageFrameVersion;
    uint16_t type = kImageFrameTypeRgba8;
    uint32_t header_size = sizeof(ImageFrameHeader);
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t layers = 0;
    uint32_t format = kImageFrameFormatRgba8;
    uint32_t bytes_per_pixel = 4;
    uint32_t reserved = 0;
    uint64_t sequence = 0;
    uint64_t monotonic_time_ns = 0;
    uint64_t payload_size = 0;
};

static_assert(sizeof(ImageFrameHeader) == 64);

struct ImageProjectionView {
    Pose pose; // The render camera in host LOCAL space, or VIEW when tagged.
    float angle_left = 0, angle_right = 0, angle_up = 0, angle_down = 0;
};
struct ImageEquirect {
    Pose pose;
    float radius, horizontal_angle, upper_angle, lower_angle;
    uint32_t eye_visibility, layer_flags;
};
struct ImageQuad {
    Pose pose;
    float width = 0, height = 0;
    uint32_t eye_visibility = 0, layer_flags = 0;
};
// Versions 4/5 carry one or two ordered quad layers in the same 96-byte
// metadata envelope. The high bit distinguishes them from stereo cameras.
constexpr uint32_t kEquirectComposition = 0x40000001u;
constexpr uint32_t kQuadCompositionBit = 0x80000000u;
constexpr uint32_t kCompositionLayerFlagsMask = 7u;
constexpr uint32_t kProjectionViewSpaceBit = 1u << 3;
struct ImageProjection {
    uint32_t view_count = 0; // Zero for legacy frames; v2 requires two eyes.
    // OpenXR core composition flags plus the transport-only VIEW-space tag.
    uint32_t layer_flags = 0;
    union {
        ImageProjectionView views[2]{};
        ImageQuad quads[2];
        ImageEquirect equirect;
    };
    // Optional trailing metadata. A 96-byte legacy envelope has identity color.
    // Two records also cover the legacy packet that packs two independent quads.
    LayerColor colors[2]{};
    bool is_equirect() const { return view_count == kEquirectComposition; }
    uint32_t quad_count() const { return (view_count & kQuadCompositionBit) ? (view_count & ~kQuadCompositionBit) : 0; }
    bool is_view_space() const {
        return view_count == 2 && (layer_flags & kProjectionViewSpaceBit) != 0;
    }
};
static_assert(sizeof(ImageQuad) == 44);
static_assert(sizeof(ImageProjectionView) == 44);
constexpr uint32_t kLegacyProjectionBytes = 96;
static_assert(sizeof(ImageProjection) == 160);
inline bool valid_layer_colors(const ImageProjection& p) {
    return p.colors[0].valid() && p.colors[1].valid();
}
inline uint32_t image_layer_flags(const ImageProjection& p, uint32_t eye) {
    return p.is_equirect() ? p.equirect.layer_flags : p.quad_count() ?
        p.quads[p.quad_count()==2 ? eye : 0].layer_flags : p.layer_flags & kCompositionLayerFlagsMask;
}

inline bool valid_projection(const ImageProjection& projection) {
    if (!valid_layer_colors(projection)) return false;
    if (projection.view_count != 2 ||
        (projection.layer_flags & ~(kCompositionLayerFlagsMask | kProjectionViewSpaceBit)) != 0) { return false; }
    for (const auto& view : projection.views) {
        const auto& p = view.pose;
        for (float value : {p.x, p.y, p.z, p.qx, p.qy, p.qz, p.qw,
                           view.angle_left, view.angle_right, view.angle_up, view.angle_down}) {
            if (!std::isfinite(value)) { return false; }
        }
        const float norm = p.qx*p.qx + p.qy*p.qy + p.qz*p.qz + p.qw*p.qw;
        if (std::fabs(norm - 1.0f) > 0.01f ||
            view.angle_left >= view.angle_right || view.angle_down >= view.angle_up ||
            view.angle_left <= -1.5707963f || view.angle_right >= 1.5707963f ||
            view.angle_down <= -1.5707963f || view.angle_up >= 1.5707963f) { return false; }
    }
    return true;
}

inline bool valid_equirect(const ImageProjection& c) {
    if (!valid_layer_colors(c)) return false;
    if (!c.is_equirect() || c.layer_flags) return false;
    const auto& e = c.equirect;
    const auto& p = e.pose;
    for (float f : {p.x,p.y,p.z,p.qx,p.qy,p.qz,p.qw,e.horizontal_angle,e.upper_angle,e.lower_angle})
        if (!std::isfinite(f)) return false;
    const float norm = p.qx*p.qx+p.qy*p.qy+p.qz*p.qz+p.qw*p.qw;
    return std::fabs(norm-1.f) <= .01f && e.radius >= 0 &&
        e.horizontal_angle >= 0 && e.horizontal_angle <= 6.2831854f &&
        e.lower_angle >= -1.5707964f && e.upper_angle <= 1.5707964f && e.lower_angle <= e.upper_angle &&
        e.eye_visibility <= 2 && !(e.layer_flags & ~kCompositionLayerFlagsMask);
}

inline bool valid_quads(const ImageProjection& composition) {
    if (!valid_layer_colors(composition)) return false;
    const auto count = composition.quad_count();
    if (count < 1 || count > 2 || composition.layer_flags) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& q = composition.quads[i];
        const auto& p = q.pose;
        for (float f : {p.x,p.y,p.z,p.qx,p.qy,p.qz,p.qw,q.width,q.height})
            if (!std::isfinite(f)) return false;
        const float norm = p.qx*p.qx+p.qy*p.qy+p.qz*p.qz+p.qw*p.qw;
        if (std::fabs(norm-1.0f)>0.01f || q.width<=0 || q.height<=0 ||
            q.eye_visibility>2 || (q.layer_flags & ~kCompositionLayerFlagsMask)) return false;
    }
    return true;
}

} // namespace axrb::protocol
