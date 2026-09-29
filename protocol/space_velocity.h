#pragma once
#include "pose_frame.h"

namespace axrb::protocol {
inline Vector3 add(Vector3 a, Vector3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vector3 subtract(Vector3 a, Vector3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vector3 cross(Vector3 a, Vector3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline Vector3 scale(Vector3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline bool finite(Vector3 a) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z); }
inline bool zero(Vector3 a) { return a.x==0 && a.y==0 && a.z==0; }
inline Vector3 rotate(const Pose& p, Vector3 v, bool inverse=false) {
    const float n=p.qx*p.qx+p.qy*p.qy+p.qz*p.qz+p.qw*p.qw;
    if (!std::isfinite(n) || n<1e-12f) return {}; // Callers require a valid orientation.
    const float s=(inverse?-1.0f:1.0f)/std::sqrt(n);
    Vector3 q{p.qx*s,p.qy*s,p.qz*s};
    auto t=scale(cross(q,v),2);
    return add(v,add(scale(t,p.qw/std::sqrt(n)),cross(q,t)));
}
inline SpaceVelocity clean_velocity(SpaceVelocity v) {
    v.flags &= 3;
    if (!finite(v.linear)) v.flags &= ~uint64_t{1};
    if (!finite(v.angular)) v.flags &= ~uint64_t{2};
    if (!(v.flags&1)) v.linear={};
    if (!(v.flags&2)) v.angular={};
    return v;
}
// Move a velocity from the tracked origin to an action-space offset.
inline SpaceVelocity offset_velocity(SpaceVelocity v, Vector3 worldOffset) {
    v=clean_velocity(v);
    if (!zero(worldOffset)) {
        if ((v.flags&3)==3) v.linear=add(v.linear,cross(v.angular,worldOffset));
        else v.flags &= ~uint64_t{1};
    }
    return clean_velocity(v);
}
// Derivative of inverse(base)*space: includes the rotating-base term, not
// just subtraction of translations. Both inputs refer to their own origins.
inline SpaceVelocity relative_velocity(SpaceVelocity v, SpaceVelocity base,
                                       const Pose& spacePose, const Pose& basePose) {
    v=clean_velocity(v); base=clean_velocity(base);
    SpaceVelocity out{};
    if ((v.flags&base.flags&2)!=0) {
        out.flags|=2;
        out.angular=rotate(basePose,subtract(v.angular,base.angular),true);
    }
    const Vector3 displacement{spacePose.x-basePose.x,spacePose.y-basePose.y,spacePose.z-basePose.z};
    if ((v.flags&base.flags&1)!=0 && ((base.flags&2) || zero(displacement))) {
        out.flags|=1;
        out.linear=rotate(basePose,subtract(subtract(v.linear,base.linear),cross(base.angular,displacement)),true);
    }
    return clean_velocity(out);
}
}
