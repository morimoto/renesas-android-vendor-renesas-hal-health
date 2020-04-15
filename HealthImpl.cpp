/*
 * Copyright (C) 2018 The Android Open Source Project
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

#define LOG_TAG "android.hardware.health@2.1-impl-renesas"
#include <android-base/logging.h>

#include <fstream>
#include <string>
#include <sys/types.h>
#include <dirent.h>
#include <memory>
#include <string_view>

#include <health/utils.h>
#include <health2impl/Health.h>
#include <hidl/Status.h>

#include "HealthImpl.h"

using ::android::sp;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::health::V2_0::Result;
using ::android::hardware::health::InitHealthdConfig;
using ::android::hardware::health::V2_1::IHealth;
using ::android::hidl::base::V1_0::IBase;
using ::android::hardware::health::V2_0::StorageInfo;
using ::android::hardware::health::V2_0::StorageAttribute;
using ::android::hardware::health::V2_0::DiskStats;
using ::android::hardware::health::V2_0::HealthInfo;
using ::android::hardware::health::V1_0::BatteryStatus;
using ::android::hardware::health::V1_0::BatteryHealth;
using namespace std::literals;

static const std::string mmc_host_dir_name("/sys/class/mmc_host");
static const std::string mmc_name_filename("name");
static const std::string mmc_type_filename("type");
static const std::string mmc_eol_filename("pre_eol_info");
static const std::string mmc_lifetime_filename("life_time");
static const std::string mmc_version_filename("rev");
static const std::string mmc_internal_type("MMC");
static const char path_separator = '/';
static const char mmc_dir_prefix[] = "mmc";
static const size_t mmc_dir_prefix_size = 3;

static const HealthInfo fakeHealthInfo {
    .legacy = {
        .chargerAcOnline       = true,
        .maxChargingCurrent    = 5000000,
        .maxChargingVoltage    = 12000000,
        .batteryPresent        = false,
        .batteryChargeCounter  = 1,
        .batteryCurrent        = 0,
        .batteryLevel          = 0,
        .batteryStatus         = BatteryStatus::UNKNOWN,
        .batteryHealth         = BatteryHealth::UNKNOWN,
        .batteryTechnology     = "AC Power",
    },
    .diskStats = std::vector<DiskStats>(),
    .storageInfos = std::vector<StorageInfo>(),
};

void process_directory(std::string directory, std::vector<std::string>& pathes) {
    std::string dir_to_open = mmc_host_dir_name + path_separator + directory;
    auto dir = opendir(dir_to_open.c_str());
    if (dir == NULL) {
        LOG(ERROR) << LOG_TAG << " Cannot open dir: " << dir_to_open;
        return;
    }
    struct dirent* entity = NULL;
    while ((entity = readdir(dir)) != NULL) {
        if (strncmp(entity->d_name, mmc_dir_prefix, mmc_dir_prefix_size) == 0) {
            std::string name = dir_to_open + path_separator + std::string(entity->d_name);
            LOG(DEBUG) << LOG_TAG << " found MMC " << name;
            pathes.push_back(name);
            break;
        }
    }
    if (closedir(dir) != 0) {
        LOG(ERROR) << LOG_TAG << " Cannot close dir: " << dir_to_open;
    }
}

void process_entity(struct dirent* entity, std::vector<std::string>& pathes) {
    if (entity->d_type == DT_DIR || entity->d_type == DT_LNK) {
        if (strncmp(entity->d_name, mmc_dir_prefix, mmc_dir_prefix_size) == 0) {
            LOG(VERBOSE) << LOG_TAG << " " << std::string(entity->d_name);
            process_directory(std::string(entity->d_name), pathes);
        }
        return;
    }

}

bool find_mmcs(std::vector<std::string>& pathes) {
    auto dir = opendir(mmc_host_dir_name.c_str());
    if (dir == NULL) {
        LOG(ERROR) << LOG_TAG << " Cannot open dir: " << mmc_host_dir_name;
        return 1;
    }
    struct dirent* entity = NULL;
    while ((entity = readdir(dir)) != NULL) {
        process_entity(entity, pathes);
    }
    if (closedir(dir) != 0) {
        LOG(ERROR) << LOG_TAG << " Cannot close dir: " << mmc_host_dir_name;
    }
    return pathes.size() == 0;
}

std::string read_file(const std::string& filename) {
    std::ifstream fs;
    std::string tmp;
    fs.open(filename);
    getline(fs, tmp);
    fs.close();
    if (tmp.length() == 0) {
        LOG(WARNING) << LOG_TAG << " File " << filename << " doesn't exist";
    }
    return tmp;
}

std::string get_mmc_name(const std::string& path) {
    return read_file(path + path_separator + mmc_name_filename);
}

std::string get_mmc_type(const std::string& path) {
    return read_file(path + path_separator + mmc_type_filename);
}

StorageAttribute get_mmc_attr(const std::string& path) {
    StorageAttribute attr;

    attr.name = get_mmc_name(path);

    std::string type = get_mmc_type(path);
    if (type.compare(mmc_internal_type) == 0) {
        attr.isInternal = true;
        attr.isBootDevice = true;
    } else {
        attr.isInternal = false;
        attr.isBootDevice = false;
    }
    return attr;
}

uint16_t get_mmc_eol(const std::string& path) {
    uint16_t eol {0};
    std::string tmp = read_file(path + path_separator + mmc_eol_filename);

    if (tmp.length() != 0) {
        eol = std::stoi(tmp, nullptr, 16);
    }

    return eol;
}

std::pair<uint16_t, uint16_t> get_mmc_lifetime(const std::string& path) {
    size_t idx {0};
    uint16_t lifetimeA {0};
    uint16_t lifetimeB {0};
    std::string tmp = read_file(path + path_separator + mmc_lifetime_filename);

    if (tmp.length() != 0) {
        lifetimeA = std::stoi(tmp, &idx, 16);
        lifetimeB = std::stoi(tmp, &idx, 16);
    }

    return {lifetimeA, lifetimeB};
}

std::string get_mmc_version(const std::string& path) {
    return read_file(path + path_separator + mmc_version_filename);
}

StorageInfo get_info(const std::string& path) {
    StorageInfo si;

    si.attr = get_mmc_attr(path);
    si.eol = get_mmc_eol(path);

    std::pair<uint16_t, uint16_t> life_time = get_mmc_lifetime(path);
    si.lifetimeA = life_time.first;
    si.lifetimeB = life_time.second;

    si.version = get_mmc_version(path);

    return si;
}

void get_storage_info(std::vector<StorageInfo>& v) {
    std::vector<std::string> mmc_pathes;
    if (find_mmcs(mmc_pathes)) {
        LOG(ERROR) << LOG_TAG << " MMC Read ERROR!";
        return;
    }
    for (auto& p : mmc_pathes) {
        StorageInfo si = get_info(p);
        v.push_back(si);
    }
}

namespace android {
namespace hardware {
namespace health {
namespace V2_1 {
namespace implementation {

template <typename T, typename Method>
static inline void GetHealthInfoField(Health* service, Method func, T* out) {
    *out = T{};
    std::invoke(func, service, [out](Result result, const T& value) {
        if (result == Result::SUCCESS) {
            *out = value;
        }
    });
}
HealthImpl::HealthImpl(std::unique_ptr<healthd_config>&& config)
    : Health(std::move(config)) {}

Return<void> HealthImpl::getStorageInfo(getStorageInfo_cb _hidl_cb) {
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
void HealthImpl::UpdateHealthInfo(HealthInfo* health_info) {
    health_info->legacy = fakeHealthInfo;

    GetHealthInfoField(this, &Health::getStorageInfo, &health_info->legacy.storageInfos);
    GetHealthInfoField(this, &Health::getDiskStats, &health_info->legacy.diskStats);

    health_info->batteryCapacityLevel = BatteryCapacityLevel::UNKNOWN;
    health_info->batteryChargeTimeToFullNowSeconds = 0;
    health_info->batteryFullChargeDesignCapacityUah = 0;
}

}  // namespace implementation
}  // namespace V2_1
}  // namespace health
}  // namespace hardware
}  // namespace android

extern "C" IHealth* HIDL_FETCH_IHealth(const char* instance) {
    using ::android::hardware::health::V2_1::implementation::HealthImpl;
    if (instance != "default"sv) {
        return nullptr;
    }
    auto config = std::make_unique<healthd_config>();
    InitHealthdConfig(config.get());

    return new HealthImpl(std::move(config));
}
