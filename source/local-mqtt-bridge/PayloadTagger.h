// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_PAYLOAD_TAGGER_H
#define AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_PAYLOAD_TAGGER_H

#include <string>

namespace Aws
{
    namespace Iot
    {
        namespace DeviceClient
        {
            namespace LocalMqttBridge
            {
                /**
                 * @brief Utility class for tagging JSON payloads with bridge direction markers
                 * 
                 * This class helps prevent message loops by tagging JSON payloads with
                 * direction markers ("up" or "down") and detecting already tagged messages.
                 */
                class PayloadTagger
                {
                public:
                    /**
                     * @brief Tag JSON payload with bridge direction
                     * 
                     * If the payload is valid JSON and doesn't already have a "_bridge" field,
                     * this method inserts {"_bridge": direction} at the top level.
                     * Non-JSON payloads are left unchanged.
                     * 
                     * @param payload The message payload to tag
                     * @param direction The bridge direction ("up" or "down")
                     * @return Tagged payload if JSON, original payload otherwise
                     */
                    static std::string tagPayload(const std::string& payload, const std::string& direction);

                    /**
                     * @brief Check if payload already has a bridge tag
                     * 
                     * @param payload The payload to check
                     * @return true if payload contains "_bridge" field, false otherwise
                     */
                    static bool isTagged(const std::string& payload);

                    /**
                     * @brief Extract bridge direction from tagged payload
                     * 
                     * @param payload The payload to extract direction from
                     * @return Bridge direction if found, empty string otherwise
                     */
                    static std::string getBridgeDirection(const std::string& payload);

                private:
                    /**
                     * @brief Check if a string is valid JSON
                     * 
                     * @param payload The string to validate
                     * @return true if the string is valid JSON, false otherwise
                     */
                    static bool isValidJson(const std::string& payload);
                };

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws

#endif // AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_PAYLOAD_TAGGER_H