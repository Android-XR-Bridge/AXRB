#pragma once
#include "pose_frame.h"
#include <cstddef>

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
// Motion over `seconds` at a constant tracking-world velocity: the
// translation and the world-frame rotation (an identity rotation if unknown).
inline Pose motion(SpaceVelocity v, float seconds) {
    v=clean_velocity(v);
    Pose d{};
    if (v.flags&1) { d.x=v.linear.x*seconds; d.y=v.linear.y*seconds; d.z=v.linear.z*seconds; }
    const Vector3 w=scale(v.angular,seconds);
    const float angle=std::sqrt(w.x*w.x+w.y*w.y+w.z*w.z);
    if ((v.flags&2) && angle>1e-6f) {
        const float s=std::sin(angle*0.5f)/angle;
        d.qx=w.x*s; d.qy=w.y*s; d.qz=w.z*s; d.qw=std::cos(angle*0.5f);
    }
    return d;
}
// Move `p` rigidly with a body at `origin` undergoing motion `d`.
inline Pose move_rigidly(const Pose& p, const Pose& origin, const Pose& d) {
    const Vector3 arm=rotate(d,{p.x-origin.x,p.y-origin.y,p.z-origin.z});
    Pose out{origin.x+d.x+arm.x,origin.y+d.y+arm.y,origin.z+d.z+arm.z};
    out.qw=d.qw*p.qw-d.qx*p.qx-d.qy*p.qy-d.qz*p.qz;
    out.qx=d.qw*p.qx+d.qx*p.qw+d.qy*p.qz-d.qz*p.qy;
    out.qy=d.qw*p.qy-d.qx*p.qz+d.qy*p.qw+d.qz*p.qx;
    out.qz=d.qw*p.qz+d.qx*p.qy-d.qy*p.qx+d.qz*p.qw;
    return out;
}
// Advance controllers, and hand joints that follow them, from the host's
// predicted display time to a later one. The head stays as sampled: the
// host compositor reprojects it, but nothing corrects a late controller.
inline void predict_controllers(PoseFrame& f, float seconds) {
    if (f.version<7 || !(seconds>0)) return;
    for (size_t hand=0; hand<2; ++hand) {
        Pose& grip=hand ? f.right_controller : f.left_controller;
        if ((f.grip_flags[hand]&3)==3) {
            const Pose before=grip;
            const Pose d=motion(f.grip_velocity[hand],seconds);
            grip=move_rigidly(before,before,d);
            auto& skeleton=f.hands[hand];
            if (skeleton.active && skeleton.source!=1) // Optical hands do not move with the grip.
                for (auto& joint : skeleton.joints) joint.pose=move_rigidly(joint.pose,before,d);
        }
        if ((f.aim_flags[hand]&3)==3)
            f.aim[hand]=move_rigidly(f.aim[hand],f.aim[hand],motion(f.aim_velocity[hand],seconds));
    }
}
}
