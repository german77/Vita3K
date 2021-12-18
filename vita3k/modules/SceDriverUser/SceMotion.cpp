// Vita3K emulator project
// Copyright (C) 2021 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include "SceMotion.h"

#include <motion/functions.h>
#include <motion/motion.h>

#include <util/log.h>

EXPORT(float, sceMotionGetAngleThreshold) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionGetBasicOrientation, SceFVector3 *basicOrientation) {
    if (!basicOrientation)
        return RET_ERROR(SCE_MOTION_ERROR_NULL_PARAMETER);

    STUBBED("set default value");
    const SceFVector3 current_acceleration = get_acceleration(host.motion);
    basicOrientation->x = current_acceleration.x;
    basicOrientation->y = current_acceleration.y;
    basicOrientation->z = current_acceleration.z;

    return 0;
}

EXPORT(SceBool, sceMotionGetDeadband) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionGetDeadbandExt) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionGetDeviceLocation, SceMotionDeviceLocation *devLocation) {
    return UNIMPLEMENTED();
}

EXPORT(SceBool, sceMotionGetGyroBiasCorrection) {
    return get_gyro_bias_correction(host.motion);
}

EXPORT(SceBool, sceMotionGetMagnetometerState) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionGetSensorState, SceMotionSensorState *sensorState, int numRecords) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionGetState, SceMotionState *motionState) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionGetStateExt) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionGetStateInternal) {
    return UNIMPLEMENTED();
}

EXPORT(SceBool, sceMotionGetTiltCorrection) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionGetTiltCorrectionExt) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionInitLibraryExt) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionMagnetometerOff) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionMagnetometerOn) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionReset) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionResetExt) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionRotateYaw, const float radians) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionSetAngleThreshold, const float angle) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionSetDeadband, SceBool setValue) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionSetDeadbandExt) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionSetGyroBiasCorrection, SceBool setValue) {
    set_gyro_bias_correction(host.motion, setValue);

    return 0;
}

EXPORT(int, sceMotionSetTiltCorrection, SceBool setValue) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionSetTiltCorrectionExt) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionStartSampling) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionStartSamplingExt) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionStopSampling) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionStopSamplingExt) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceMotionTermLibraryExt) {
    return UNIMPLEMENTED();
}

BRIDGE_IMPL(sceMotionGetAngleThreshold)
BRIDGE_IMPL(sceMotionGetBasicOrientation)
BRIDGE_IMPL(sceMotionGetDeadband)
BRIDGE_IMPL(sceMotionGetDeadbandExt)
BRIDGE_IMPL(sceMotionGetDeviceLocation)
BRIDGE_IMPL(sceMotionGetGyroBiasCorrection)
BRIDGE_IMPL(sceMotionGetMagnetometerState)
BRIDGE_IMPL(sceMotionGetSensorState)
BRIDGE_IMPL(sceMotionGetState)
BRIDGE_IMPL(sceMotionGetStateExt)
BRIDGE_IMPL(sceMotionGetStateInternal)
BRIDGE_IMPL(sceMotionGetTiltCorrection)
BRIDGE_IMPL(sceMotionGetTiltCorrectionExt)
BRIDGE_IMPL(sceMotionInitLibraryExt)
BRIDGE_IMPL(sceMotionMagnetometerOff)
BRIDGE_IMPL(sceMotionMagnetometerOn)
BRIDGE_IMPL(sceMotionReset)
BRIDGE_IMPL(sceMotionResetExt)
BRIDGE_IMPL(sceMotionRotateYaw)
BRIDGE_IMPL(sceMotionSetAngleThreshold)
BRIDGE_IMPL(sceMotionSetDeadband)
BRIDGE_IMPL(sceMotionSetDeadbandExt)
BRIDGE_IMPL(sceMotionSetGyroBiasCorrection)
BRIDGE_IMPL(sceMotionSetTiltCorrection)
BRIDGE_IMPL(sceMotionSetTiltCorrectionExt)
BRIDGE_IMPL(sceMotionStartSampling)
BRIDGE_IMPL(sceMotionStartSamplingExt)
BRIDGE_IMPL(sceMotionStopSampling)
BRIDGE_IMPL(sceMotionStopSamplingExt)
BRIDGE_IMPL(sceMotionTermLibraryExt)
