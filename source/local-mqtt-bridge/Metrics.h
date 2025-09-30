// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_METRICS_H
#define AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_METRICS_H

#include <atomic>
#include <cstdint>
#include <aws/crt/JsonObject.h>
#include <chrono>
#include <string>

namespace Aws
{
    namespace Iot
    {
        namespace DeviceClient
        {
            namespace LocalMqttBridge
            {
                struct BridgeMetricsConfig
                {
                    bool enabled {false};
                    uint32_t publishIntervalSec {60};
                    std::string awsTopic; // metrics publishing topic
                };

                class Metrics
                {
                public:
                    explicit Metrics(const BridgeMetricsConfig &cfg, const std::string &thingName)
                        : config(cfg), start(std::chrono::steady_clock::now()), thingName(thingName) {}

                    // counters
                    std::atomic<uint64_t> forwardedUp {0};
                    std::atomic<uint64_t> forwardedDown {0};
                    std::atomic<uint64_t> throttledMessages {0};
                    std::atomic<uint64_t> droppedLoop {0};
                    std::atomic<uint64_t> droppedQos2 {0};
                    std::atomic<uint64_t> queuedOffline {0};
                    std::atomic<uint64_t> publishErrors {0};
                    std::atomic<uint64_t> droppedOverflow {0};

                    BridgeMetricsConfig getConfig() const { return config; }

                    Aws::Crt::String toJson(uint32_t offlineDepth, uint32_t localQueueDepth, uint32_t awsQueueDepth) const;

                private:
                    BridgeMetricsConfig config;
                    std::chrono::steady_clock::time_point start;
                    std::string thingName;
                };
            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws

#endif // AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_METRICS_H
