// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_LOOP_GUARD_H
#define AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_LOOP_GUARD_H

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace Aws
{
    namespace Iot
    {
        namespace DeviceClient
        {
            namespace LocalMqttBridge
            {
                /**
                 * @brief Prevents message loops by tracking recently processed messages
                 * 
                 * Uses message signatures (direction + topic + payload hash) with TTL
                 * to detect and drop duplicate messages within the configured time window.
                 */
                class LoopGuard
                {
                public:
                    /**
                     * @brief Constructor
                     * 
                     * @param ttlSeconds Time-to-live for tracked messages in seconds
                     * @param maxEntries Maximum number of entries to track (prevents memory growth)
                     */
                    LoopGuard(int ttlSeconds, int maxEntries);

                    /**
                     * @brief Destructor
                     */
                    ~LoopGuard();

                    /**
                     * @brief Check if message should be dropped as duplicate
                     * 
                     * @param direction Bridge direction ("up" or "down")
                     * @param topic Message topic
                     * @param payload Message payload
                     * @return true if message should be dropped (is duplicate within TTL)
                     */
                    bool shouldDrop(const std::string& direction, const std::string& topic, 
                                   const std::string& payload);

                    /**
                     * @brief Get current number of tracked entries
                     * 
                     * @return Number of entries currently being tracked
                     */
                    size_t getEntryCount() const;

                    /**
                     * @brief Clear all tracked entries
                     */
                    void clear();

                private:
                    struct Entry
                    {
                        std::chrono::steady_clock::time_point timestamp;
                        std::string key;
                    };

                    int ttlSeconds;
                    int maxEntries;
                    std::unordered_map<std::string, Entry> entries;
                    mutable std::mutex mutex;

                    static constexpr char TAG[] = "LoopGuard.cpp";

                    /**
                     * @brief Generate unique key for message signature
                     * 
                     * @param direction Bridge direction
                     * @param topic Message topic
                     * @param payload Message payload
                     * @return Unique key string
                     */
                    std::string generateKey(const std::string& direction, const std::string& topic,
                                           const std::string& payload) const;

                    /**
                     * @brief Remove expired entries from tracking
                     * 
                     * Called periodically to prevent memory growth
                     */
                    void cleanup();

                    /**
                     * @brief Generate hash of payload for signature
                     * 
                     * Uses first 256 bytes of payload to generate hash
                     * 
                     * @param payload The payload to hash
                     * @return Hash string
                     */
                    std::string hashPayload(const std::string& payload) const;
                };

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws

#endif // AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_LOOP_GUARD_H