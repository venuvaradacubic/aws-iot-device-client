// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_CONFIG_MODEL_H
#define AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_CONFIG_MODEL_H

#include "../config/Config.h"

namespace Aws
{
    namespace Iot
    {
        namespace DeviceClient
        {
            namespace LocalMqttBridge
            {
                // Re-export config types for convenience
                using LocalMqttBridgeConfig = PlainConfig::LocalMqttBridge;
                using LocalBroker = PlainConfig::LocalMqttBridge::LocalBroker;
                using Route = PlainConfig::LocalMqttBridge::Route;
                using QueueConfig = PlainConfig::LocalMqttBridge::QueueConfig;
                using LoopGuardConfig = PlainConfig::LocalMqttBridge::LoopGuardConfig;
                using MetricsConfig = PlainConfig::LocalMqttBridge::MetricsConfig;

                /**
                 * @brief Validates that a route direction is valid
                 * @param direction The direction string to validate
                 * @return true if direction is "up" or "down", false otherwise
                 */
                bool isValidDirection(const std::string& direction);

                /**
                 * @brief Validates that QoS value is supported
                 * @param qos The QoS value to validate
                 * @return true if qos is 0 or 1, false otherwise
                 */
                bool isValidQoS(int qos);

                /**
                 * @brief Expands variables in a topic template
                 * @param template_str The template string with variables like ${thingName}, ${match1}, ${tail}
                 * @param thingName The thing name to substitute
                 * @param variables Map of variable names to values for expansion
                 * @return Expanded topic string
                 */
                std::string expandTopicTemplate(const std::string& template_str, 
                                               const std::string& thingName,
                                               const std::map<std::string, std::string>& variables = {});

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws

#endif // AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_CONFIG_MODEL_H