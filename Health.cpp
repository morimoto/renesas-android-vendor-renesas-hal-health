/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "HealthHAL"

#include <Health.h>

namespace android {
namespace hardware {
namespace health {
namespace V1_0 {
namespace renesas {

// Methods from ::android::hardware::health::V1_0::IHealth follow.
Return<void> Health::init(const HealthConfig& config, init_cb _hidl_cb)  {
    _hidl_cb(config);

    return Void();
}

Return<void> Health::update(const HealthInfo& /*info*/, update_cb _hidl_cb)  {

    HealthInfo healthInfo = {
        .chargerAcOnline       = true,
        .chargerUsbOnline      = false,
        .chargerWirelessOnline = false,
        .maxChargingCurrent    = 0,
        .maxChargingVoltage    = 0,
        .batteryPresent        = false,
        .batteryLevel          = 100,
        .batteryVoltage        = 0,
        .batteryTemperature    = 0,
        .batteryCurrent        = 0,
        .batteryCycleCount     = 0,
        .batteryFullCharge     = 0,
        .batteryChargeCounter  = 0,
        .batteryStatus         = android::hardware::health::V1_0::BatteryStatus::FULL,
        .batteryHealth         = android::hardware::health::V1_0::BatteryHealth::GOOD,
        .batteryTechnology     = "AC Power",
    };

    _hidl_cb(0, healthInfo);

    return Void();
}

Return<void> Health::energyCounter(energyCounter_cb _hidl_cb) {
    _hidl_cb(Result::SUCCESS, 100);

   return Void();
}

} // namespace renesas
}  // namespace V1_0
}  // namespace health
}  // namespace hardware
}  // namespace android
