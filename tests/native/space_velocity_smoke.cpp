#include "space_velocity.h"
#include <cassert>
#include <limits>
#include <cstddef>
using namespace axrb::protocol;
static void near(Vector3 a, Vector3 b) {
    assert(std::abs(a.x-b.x)<1e-5 && std::abs(a.y-b.y)<1e-5 && std::abs(a.z-b.z)<1e-5);
}
int main() {
    static_assert(offsetof(PoseFrame,hmd_velocity)==2448);
    Pose origin{}, hand{1,0,0};
    SpaceVelocity fixed{3,{},{}}, punch{3,{2,0,-3},{0,1,0}};
    auto v=relative_velocity(punch,fixed,hand,origin);
    assert(v.flags==3); near(v.linear,{2,0,-3}); near(v.angular,{0,1,0});
    // Rotation of axes must apply equally to linear and angular velocity.
    Pose baseRot{0,0,0,0,0,0.70710678f,0.70710678f};
    v=relative_velocity(punch,fixed,hand,baseRot);
    near(v.linear,{0,-2,-3}); near(v.angular,{1,0,0});
    // A rotating base observes tangential movement of a stationary hand.
    v=relative_velocity(fixed,{3,{}, {0,0,2}},hand,origin);
    near(v.linear,{0,-2,0}); near(v.angular,{0,0,-2});
    v=relative_velocity(punch,{3,{1,0,0},{}},hand,origin);
    near(v.linear,{1,0,-3});
    v=offset_velocity({3,{1,0,0},{0,0,2}},{0.5f,0,0});
    assert(v.flags==3); near(v.linear,{1,1,0});
    v=offset_velocity({1,{1,0,0},{}},{0.5f,0,0});
    assert(v.flags==0); near(v.linear,{});
    v=offset_velocity({1,{1,0,0},{}},{});
    assert(v.flags==1); near(v.linear,{1,0,0});
    v=relative_velocity(punch,{},hand,origin); assert(v.flags==0);
    v=relative_velocity({1,{1,0,0},{}},fixed,hand,origin); assert(v.flags==1);
    v=relative_velocity(punch,{1,{},{0,0,99}},hand,origin); assert(v.flags==0);
    v=clean_velocity({3,{std::numeric_limits<float>::quiet_NaN(),0,0},{0,1,0}});
    assert(v.flags==2); near(v.linear,{});
    // Quaternion normalization protects against small host precision errors.
    baseRot.qz*=2; baseRot.qw*=2;
    near(rotate(baseRot,{1,0,0}),{0,1,0});
    return 0;
}
