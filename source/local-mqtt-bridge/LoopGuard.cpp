// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LoopGuard.h"
#include "../logging/LoggerFactory.h"
#include <functional>
#include <sstream>
#include <iomanip>

using namespace std;
using namespace Aws::Iot::DeviceClient::LocalMqttBridge;
using namespace Aws::Iot::DeviceClient::Logging;

namespace Aws
{
    namespace Iot
    {
        namespace DeviceClient
        {
            namespace LocalMqttBridge
            {
                constexpr char LoopGuard::TAG[];

                LoopGuard::LoopGuard(int ttlSeconds, int maxEntries)
                    : ttlSeconds(ttlSeconds), maxEntries(maxEntries)
                {
                    if (ttlSeconds <= 0)
                    {
                        this->ttlSeconds = 5; // Default fallback
                        LOGM_WARN(TAG, "Invalid TTL %d, using default %d seconds", ttlSeconds, this->ttlSeconds);
                    }
                    
                    if (maxEntries <= 0)
                    {
                        this->maxEntries = 512; // Default fallback
                        LOGM_WARN(TAG, "Invalid maxEntries %d, using default %d", maxEntries, this->maxEntries);
                    }

                    LOGM_INFO(TAG, "LoopGuard initialized with TTL=%d seconds, maxEntries=%d", 
                             this->ttlSeconds, this->maxEntries);
                }

                LoopGuard::~LoopGuard() = default;

                bool LoopGuard::shouldDrop(const std::string& direction, const std::string& topic, 
                                          const std::string& payload)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    
                    std::string key = generateKey(direction, topic, payload);
                    auto now = std::chrono::steady_clock::now();

                    // Check if we've seen this message recently
                    auto it = entries.find(key);
                    if (it != entries.end())
                    {
                        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp);
                        if (age.count() < ttlSeconds)
                        {
                            LOGM_DEBUG(TAG, "Dropping duplicate message: direction=%s, topic=%s, age=%ld seconds", 
                                      direction.c_str(), topic.c_str(), age.count());
                            return true;
                        }
                        else
                        {
                            // Expired entry, remove it
                            entries.erase(it);
                        }
                    }

                    // Add new entry
                    Entry entry;
                    entry.timestamp = now;
                    entry.key = key;
                    entries[key] = entry;

                    // Cleanup if we have too many entries
                    if (entries.size() > static_cast<size_t>(maxEntries))
                    {
                        cleanup();
                    }

                    return false;
                }

                size_t LoopGuard::getEntryCount() const
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    return entries.size();
                }

                void LoopGuard::clear()
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    entries.clear();
                    LOGM_INFO(TAG, "LoopGuard entries cleared");
                }

                std::string LoopGuard::generateKey(const std::string& direction, const std::string& topic,
                                                  const std::string& payload) const
                {
                    std::string payloadHash = hashPayload(payload);
                    return direction + "|" + topic + "|" + payloadHash;
                }

                void LoopGuard::cleanup()
                {
                    auto now = std::chrono::steady_clock::now();
                    size_t removedCount = 0;
                    
                    for (auto it = entries.begin(); it != entries.end();)
                    {
                        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp);
                        if (age.count() >= ttlSeconds)
                        {
                            it = entries.erase(it);
                            removedCount++;
                        }
                        else
                        {
                            ++it;
                        }
                    }

                    // If still too many entries after cleanup, remove oldest ones
                    while (entries.size() > static_cast<size_t>(maxEntries))
                    {
                        auto oldest = entries.begin();
                        for (auto it = entries.begin(); it != entries.end(); ++it)
                        {
                            if (it->second.timestamp < oldest->second.timestamp)
                            {
                                oldest = it;
                            }
                        }
                        entries.erase(oldest);
                        removedCount++;
                    }

                    if (removedCount > 0)
                    {
                        LOGM_DEBUG(TAG, "Cleanup removed %zu expired/oldest entries, %zu entries remaining", 
                                  removedCount, entries.size());
                    }
                }

                std::string LoopGuard::hashPayload(const std::string& payload) const
                {
                    // Use first 256 bytes of payload for hash to avoid processing huge payloads
                    std::string truncated = payload.substr(0, 256);
                    
                    // Simple hash using std::hash
                    std::hash<std::string> hasher;
                    size_t hashValue = hasher(truncated);
                    
                    // Convert to hex string
                    std::stringstream ss;
                    ss << std::hex << hashValue;
                    return ss.str();
                }

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws