#pragma once

#include <algorithm>

namespace RBX {

// Torque is an authored upper bound. A discrete motor must not brake through
// zero, or accelerate its wheel past the requested surface speed in one step.
inline float limitLegacyVehicleMotorTorque(float torque, float directionalSpeed,
    float angularSpeedLimit, float inverseInertia, float dt, bool driving)
{
    if (!(torque > 0.0f) || !(inverseInertia > 0.0f) || !(dt > 0.0f))
        return torque;
    const float speedChange = directionalSpeed < 0.0f ? -directionalSpeed :
        (driving ? angularSpeedLimit - directionalSpeed : 0.0f);
    return std::min(torque, std::max(0.0f, speedChange) / (dt * inverseInertia));
}

}
