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
#define LOG_TAG "android.hardware.health@2.0-impl"
#include <android-base/logging.h>

#include <android-base/file.h>
#include <HealthImpl.h>

#include <hal_conversion.h>
#include <hidl/HidlTransportSupport.h>

extern void healthd_battery_update_internal(bool);

namespace android {
namespace hardware {
namespace health {
namespace V2_0 {
namespace renesas {

sp<Health> Health::instance_;

static const V2_0::HealthInfo fakeHealthInfo {
    .legacy = {
        .chargerAcOnline       = true,
        .maxChargingCurrent    = 5000000,
        .maxChargingVoltage    = 12000000,
        .batteryPresent        = false,
        .batteryChargeCounter  = 1,
        .batteryCurrent        = 0,
        .batteryLevel          = 0,
        .batteryStatus         = V1_0::BatteryStatus::UNKNOWN,
        .batteryHealth         = V1_0::BatteryHealth::UNKNOWN,
        .batteryTechnology     = "AC Power",
    },
    .diskStats = std::vector<DiskStats>(),
    .storageInfos = std::vector<StorageInfo>(),
};

Health::Health(struct healthd_config* c) {
    battery_monitor_ = std::make_unique<BatteryMonitor>();
    battery_monitor_->init(c);
}

// Methods from IHealth follow.
Return<Result> Health::registerCallback(const sp<IHealthInfoCallback>& callback) {
    if (callback == nullptr) {
        return Result::SUCCESS;
    }

    {
        std::lock_guard<std::mutex> _lock(callbacks_lock_);
        callbacks_.push_back(callback);
        // unlock
    }

    auto linkRet = callback->linkToDeath(this, 0u /* cookie */);
    if (!linkRet.withDefault(false)) {
        LOG(WARNING) << __func__ << "Cannot link to death: "
                     << (linkRet.isOk() ? "linkToDeath returns false" : linkRet.description());
        // ignore the error
    }

    return update();
}

bool Health::unregisterCallbackInternal(const sp<IBase>& callback) {
    if (callback == nullptr) {
        return false;
    }

    bool removed = false;
    std::lock_guard<std::mutex> _lock(callbacks_lock_);
    for (auto it = callbacks_.begin(); it != callbacks_.end();) {
        if (interfacesEqual(*it, callback)) {
            it = callbacks_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    (void)callback->unlinkToDeath(this).isOk();  // ignore errors
    return removed;
}

Return<Result> Health::unregisterCallback(const sp<IHealthInfoCallback>& callback) {
    return unregisterCallbackInternal(callback) ? Result::SUCCESS : Result::NOT_FOUND;
}

template <typename T>
void getProperty(const std::unique_ptr<BatteryMonitor>& monitor __unused,
                 int id __unused, T defaultValue,
                 const std::function<void(Result, T)>& callback) {
    T ret = defaultValue;
    Result result = Result::SUCCESS;
    callback(result, static_cast<T>(ret));
}

Return<void> Health::getChargeCounter(getChargeCounter_cb _hidl_cb) {
    getProperty<int32_t>(battery_monitor_, BATTERY_PROP_CHARGE_COUNTER, 1, _hidl_cb);
    return Void();
}

Return<void> Health::getCurrentNow(getCurrentNow_cb _hidl_cb) {
    getProperty<int32_t>(battery_monitor_, BATTERY_PROP_CURRENT_NOW, 1, _hidl_cb);
    return Void();
}

Return<void> Health::getCurrentAverage(getCurrentAverage_cb _hidl_cb) {
    getProperty<int32_t>(battery_monitor_, BATTERY_PROP_CURRENT_AVG, 1, _hidl_cb);
    return Void();
}

Return<void> Health::getCapacity(getCapacity_cb _hidl_cb) {
    getProperty<int32_t>(battery_monitor_, BATTERY_PROP_CAPACITY, 1, _hidl_cb);
    return Void();
}

Return<void> Health::getEnergyCounter(getEnergyCounter_cb _hidl_cb) {
    getProperty<int64_t>(battery_monitor_, BATTERY_PROP_ENERGY_COUNTER, 1, _hidl_cb);
    return Void();
}

Return<void> Health::getChargeStatus(getChargeStatus_cb _hidl_cb) {
    getProperty(battery_monitor_, BATTERY_PROP_BATTERY_STATUS, BatteryStatus::FULL, _hidl_cb);
    return Void();
}

Return<Result> Health::update() {
    if (!healthd_mode_ops || !healthd_mode_ops->battery_update) {
        LOG(WARNING) << "health@2.0: update: not initialized. "
                     << "update() should not be called in charger / recovery.";
        return Result::UNKNOWN;
    }

    // Retrieve all information and call healthd_mode_ops->battery_update, which calls
    // notifyListeners.
    bool chargerOnline = battery_monitor_->update();

    // adjust uevent / wakealarm periods
    healthd_battery_update_internal(chargerOnline);

    return Result::SUCCESS;
}

void Health::notifyListeners(HealthInfo* healthInfo __unused) {
    std::lock_guard<std::mutex> _lock(callbacks_lock_);
    for (auto it = callbacks_.begin(); it != callbacks_.end();) {
        auto ret = (*it)->healthInfoChanged(fakeHealthInfo);
        if (!ret.isOk() && ret.isDeadObject()) {
            it = callbacks_.erase(it);
        } else {
            ++it;
        }
    }
}

Return<void> Health::debug(const hidl_handle& handle, const hidl_vec<hidl_string>&) {
    if (handle != nullptr && handle->numFds >= 1) {
        int fd = handle->data[0];
        battery_monitor_->dumpState(fd);

        getHealthInfo([fd](auto res, const auto& info) {
            android::base::WriteStringToFd("\ngetHealthInfo -> ", fd);
            if (res == Result::SUCCESS) {
                android::base::WriteStringToFd(toString(info), fd);
            } else {
                android::base::WriteStringToFd(toString(res), fd);
            }
            android::base::WriteStringToFd("\n", fd);
        });

        fsync(fd);
    }
    return Void();
}

Return<void> Health::getStorageInfo(getStorageInfo_cb _hidl_cb) {
    std::vector<struct StorageInfo> info;
    get_storage_info(info);
    hidl_vec<struct StorageInfo> info_vec(info);
    if (!info.size()) {
        _hidl_cb(Result::NOT_SUPPORTED, info_vec);
    } else {
        _hidl_cb(Result::SUCCESS, info_vec);
    }
    return Void();
}

Return<void> Health::getDiskStats(getDiskStats_cb _hidl_cb) {
    std::vector<struct DiskStats> stats;
    get_disk_stats(stats);
    hidl_vec<struct DiskStats> stats_vec(stats);
    if (!stats.size()) {
        _hidl_cb(Result::NOT_SUPPORTED, stats_vec);
    } else {
        _hidl_cb(Result::SUCCESS, stats_vec);
    }
    return Void();
}

Return<void> Health::getHealthInfo(getHealthInfo_cb _hidl_cb) {
    V2_0::HealthInfo healthInfo(fakeHealthInfo);
    std::vector<StorageInfo> info;
    get_storage_info(info);
    healthInfo.storageInfos = info;
    _hidl_cb(Result::SUCCESS, healthInfo);
    return Void();
}

void Health::serviceDied(uint64_t /* cookie */, const wp<IBase>& who) {
    (void)unregisterCallbackInternal(who.promote());
}

sp<IHealth> Health::initInstance(struct healthd_config* c) {
    if (instance_ == nullptr) {
        instance_ = new Health(c);
    }
    return instance_;
}

sp<Health> Health::getImplementation() {
    CHECK(instance_ != nullptr);
    return instance_;
}

}  // namespace renesas
}  // namespace V2_0
}  // namespace health
}  // namespace hardware
}  // namespace android
