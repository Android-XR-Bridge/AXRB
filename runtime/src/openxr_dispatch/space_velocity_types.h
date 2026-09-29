#pragma once
#include "hand_tracking_types.h"
constexpr XrStructureType XR_TYPE_SPACE_VELOCITY = static_cast<XrStructureType>(43);
constexpr XrSpaceVelocityFlags XR_SPACE_VELOCITY_LINEAR_VALID_BIT = 1;
constexpr XrSpaceVelocityFlags XR_SPACE_VELOCITY_ANGULAR_VALID_BIT = 2;
struct XrSpaceVelocity {
    XrStructureType type;
    void* next;
    XrSpaceVelocityFlags velocityFlags;
    XrVector3f linearVelocity;
    XrVector3f angularVelocity;
};
