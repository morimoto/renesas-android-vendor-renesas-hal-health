/*
 * Copyright 2018 The Android Open Source Project
 * Copyright 2018 GlobalLogic
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

#include <fstream>
#include <string>
#include <sys/types.h>
#include <dirent.h>

#include <android-base/logging.h>

#include <android/hardware/health/1.0/types.h>
#include <hal_conversion.h>
#include <HealthImpl.h>
#include <healthd/healthd.h>
#include <hidl/HidlTransportSupport.h>
#include <hwbinder/IPCThreadState.h>

using android::hardware::IPCThreadState;
using android::hardware::configureRpcThreadpool;
using android::hardware::handleTransportPoll;
using android::hardware::setupTransportPolling;
using android::hardware::health::V2_0::HealthInfo;
using android::hardware::health::V1_0::hal_conversion::convertToHealthInfo;
using android::hardware::health::V2_0::IHealth;
using android::hardware::health::V2_0::renesas::Health;
using android::hardware::health::V2_0::StorageAttribute;
using android::hardware::health::V2_0::StorageInfo;

static const std::string mmc_host_dir_name("/sys/class/mmc_host");


extern int healthd_main(void);

static int gBinderFd = -1;
static std::string gInstanceName;

static void binder_event(uint32_t /*epevents*/) {
    if (gBinderFd >= 0) {
        handleTransportPoll(gBinderFd);
    }
}

void healthd_mode_service_2_0_init(struct healthd_config* config) {
    LOG(INFO) << LOG_TAG << gInstanceName << " Hal is starting up...";

    gBinderFd = setupTransportPolling();

    if (gBinderFd >= 0) {
        if (healthd_register_event(gBinderFd, binder_event)) {
            LOG(ERROR) << LOG_TAG << gInstanceName << ": Register for binder events failed";
        }
    }

    android::sp<IHealth> service = Health::initInstance(config);
    CHECK_EQ(service->registerAsService(gInstanceName), android::OK)
        << LOG_TAG << gInstanceName << ": Failed to register HAL";

    LOG(INFO) << LOG_TAG << gInstanceName << ": Hal init done";
}

int healthd_mode_service_2_0_preparetowait(void) {
    IPCThreadState::self()->flushCommands();
    return -1;
}

void healthd_mode_service_2_0_heartbeat(void) {
    // noop
}

void healthd_mode_service_2_0_battery_update(struct android::BatteryProperties* prop) {
    HealthInfo info;
    convertToHealthInfo(prop, info.legacy);
    Health::getImplementation()->notifyListeners(&info);
}

static struct healthd_mode_ops healthd_mode_service_2_0_ops = {
    .init = healthd_mode_service_2_0_init,
    .preparetowait = healthd_mode_service_2_0_preparetowait,
    .heartbeat = healthd_mode_service_2_0_heartbeat,
    .battery_update = healthd_mode_service_2_0_battery_update,
};

void healthd_board_init(struct healthd_config*) {}

int healthd_board_battery_update(struct android::BatteryProperties*) {
    // return 0 to log periodic polled battery status to kernel log
    return 0;
}

void process_directory(std::string directory, std::vector<std::string>& pathes) {
    std::string dir_to_open = mmc_host_dir_name + "/" + directory;
    auto dir = opendir(dir_to_open.c_str());
    auto entity = readdir(dir);
    while(entity != NULL) {
        if(strncmp(entity->d_name, "mmc", 3) == 0) {
            std::string name = dir_to_open + "/" + std::string(entity->d_name);
            LOG(DEBUG) << LOG_TAG << " found MMC " << name;
            pathes.push_back(name);
            return;
        }
        entity = readdir(dir);
    }
}

void process_entity(struct dirent* entity, std::vector<std::string>& pathes) {
    if(entity->d_type == DT_DIR || entity->d_type == DT_LNK) {
        if(strncmp(entity->d_name, "mmc", 3) == 0) {
            LOG(VERBOSE) << LOG_TAG << " " << std::string(entity->d_name);
            process_directory(std::string(entity->d_name), pathes);
        }
        return;
    }

}

bool find_mmcs(std::vector<std::string>& pathes) {
    auto dir = opendir(mmc_host_dir_name.c_str());
    auto entity = readdir(dir);
    while(entity != NULL) {
        process_entity(entity, pathes);
        entity = readdir(dir);
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
    return read_file(path + "/name");
}

std::string get_mmc_type(const std::string& path) {
    return read_file(path + "/type");
}

StorageAttribute get_mmc_attr(const std::string& path) {
    StorageAttribute attr;

    attr.name = get_mmc_name(path);

    std::string type = get_mmc_type(path);
    if (type.compare("MMC") == 0) {
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
    std::string tmp = read_file(path + "/pre_eol_info");

    if (tmp.length() != 0) {
        eol = std::stoi(tmp, nullptr, 16);
    }

    return eol;
}

std::pair<uint16_t, uint16_t> get_mmc_lifetime(const std::string& path) {
    size_t idx {0};
    uint16_t lifetimeA {0};
    uint16_t lifetimeB {0};
    std::string tmp = read_file(path + "/life_time");

    if (tmp.length() != 0) {
        lifetimeA = std::stoi(tmp, &idx, 16);
        lifetimeB = std::stoi(tmp, &idx, 16);
    }

    return {lifetimeA, lifetimeB};
}

std::string get_mmc_version(const std::string& path) {
    return read_file(path + "/rev");
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
    for(auto& p : mmc_pathes) {
        StorageInfo si = get_info(p);
        v.push_back(si);
    }
}
void get_disk_stats(std::vector<struct DiskStats>&) {
    // ...
}

int main()
{
    if (gInstanceName.empty()) {
        gInstanceName = "default";
    }
    healthd_mode_ops = &healthd_mode_service_2_0_ops;
    return healthd_main();
}
