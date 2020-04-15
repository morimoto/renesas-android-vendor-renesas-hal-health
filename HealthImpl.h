/*
 * Copyright (C) 2019 The Android Open Source Project
 * Copyright (C) 2020 GlobalLogic
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
#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <android-base/unique_fd.h>
#include <android/hardware/health/2.1/IHealth.h>
#include <healthd/BatteryMonitor.h>
#include <hidl/Status.h>

#include <health2impl/Callback.h>

namespace android {
namespace hardware {
namespace health {
namespace V2_1 {
namespace implementation {

class HealthImpl : public Health {
public:
    HealthImpl(std::unique_ptr<healthd_config>&& config);
    Return<void> getStorageInfo(getStorageInfo_cb _hidl_cb) override;
protected:
    void UpdateHealthInfo(HealthInfo* health_info) override;
};

}  // namespace implementation
}  // namespace V2_1
}  // namespace health
}  // namespace hardware
}  // namespace android
