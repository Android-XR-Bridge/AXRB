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
    // Controller prediction: a quarter turn about +Z in 0.5 s while moving +X.
    PoseFrame f{};
    f.left_controller={1,0,0};
    f.grip_flags[0]=3;
    f.grip_velocity[0]={3,{2,0,0},{0,0,3.14159265f}};
    f.hands[0].active=1; f.hands[0].source=2;
    f.hands[0].joints[0].pose={1,1,0};
    f.aim[1]={0,1,0}; f.aim_flags[1]=3; f.aim_velocity[1]={1,{0,0,-4},{}};
    f.right_controller={5,5,5}; f.grip_velocity[1]={3,{1,1,1},{}}; // Untracked: unchanged.
    predict_controllers(f,0.5f);
    near({f.left_controller.x,f.left_controller.y,f.left_controller.z},{2,0,0});
    assert(std::abs(f.left_controller.qz-0.70710678f)<1e-5 && std::abs(f.left_controller.qw-0.70710678f)<1e-5);
    // A joint 1 m along +Y of the grip swings to -X with it, then translates.
    near({f.hands[0].joints[0].pose.x,f.hands[0].joints[0].pose.y,f.hands[0].joints[0].pose.z},{1,0,0});
    near({f.aim[1].x,f.aim[1].y,f.aim[1].z},{0,1,-2});
    assert(f.aim[1].qw==1);
    near({f.right_controller.x,f.right_controller.y,f.right_controller.z},{5,5,5});
    // Optical hands are not carried by the grip; older frames are untouched.
    PoseFrame optical{}; optical.grip_flags[0]=3; optical.grip_velocity[0]={1,{1,0,0},{}};
    optical.hands[0].active=1; optical.hands[0].source=1; optical.hands[0].joints[0].pose={0,1,0};
    predict_controllers(optical,1);
    near({optical.hands[0].joints[0].pose.x,optical.hands[0].joints[0].pose.y,0},{0,1,0});
    near({optical.left_controller.x,0,0},{1,0,0});
    optical.version=6; predict_controllers(optical,1);
    near({optical.left_controller.x,0,0},{1,0,0});
    return 0;
}
