/*
 * Copyright 2018 The Android Open Source Project
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

#include <android/hardware/health/1.0/IHealth.h>
#include <android-base/logging.h>
#include <android-base/macros.h>

#include <hidl/HidlTransportSupport.h>

#include "Health.h"

using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;

using android::hardware::health::V1_0::IHealth;
using android::hardware::health::V1_0::renesas::Health;

int main() {

    configureRpcThreadpool(1, true /* callerWillJoin */);

    android::sp<IHealth> health = new Health;

    android::status_t status = health->registerAsService();
    LOG_ALWAYS_FATAL_IF(status != android::OK, "Could not register IHealth");

    joinRpcThreadpool();
}
