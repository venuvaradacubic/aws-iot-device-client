// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Queue.h"
#include "../logging/LoggerFactory.h"
#include <algorithm>
#include <regex>

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
                constexpr char Queue::TAG[];

                Queue::Queue(size_t maxSize, int heartbeatWindow)
                    : maxSize(maxSize), heartbeatWindow(heartbeatWindow),
                      lastHeartbeatCleanup(std::chrono::steady_clock::now())
                {
                    if (maxSize == 0)
                    {
                        this->maxSize = 1000; // Default fallback
                        LOGM_WARN(TAG, "Invalid maxSize 0, using default %zu", this->maxSize);
                    }
                    
                    if (heartbeatWindow <= 0)
                    {
                        this->heartbeatWindow = 5; // Default fallback
                        LOGM_WARN(TAG, "Invalid heartbeatWindow %d, using default %d seconds", 
                                 heartbeatWindow, this->heartbeatWindow);
                    }

                    LOGM_INFO(TAG, "Queue initialized with maxSize=%zu, heartbeatWindow=%d seconds", 
                             this->maxSize, this->heartbeatWindow);
                }

                Queue::~Queue()
                {
                    clear();
                }

                bool Queue::push(const QueuedMessage& message)
                {
                    std::lock_guard<std::mutex> lock(mutex);

                    // Check for heartbeat deduplication
                    if (isHeartbeatTopic(message.topic) && isDuplicateHeartbeat(message.topic))
                    {
                        LOGM_DEBUG(TAG, "Dropping duplicate heartbeat message on topic: %s", message.topic.c_str());
                        return false;
                    }

                    // Check queue capacity
                    if (queue.size() >= maxSize)
                    {
                        LOGM_WARN(TAG, "Queue full (size=%zu), dropping message on topic: %s", 
                                 queue.size(), message.topic.c_str());
                        return false;
                    }

                    queue.push(message);
                    condition.notify_one();
                    
                    LOGM_DEBUG(TAG, "Message queued: topic=%s, payload_size=%zu, queue_size=%zu", 
                              message.topic.c_str(), message.payload.size(), queue.size());
                    return true;
                }

                bool Queue::push(const std::string& topic, const std::string& payload, bool isFromLocal)
                {
                    QueuedMessage message(topic, payload, isFromLocal);
                    return push(message);
                }

                bool Queue::pop(QueuedMessage& message, int timeoutMs)
                {
                    std::unique_lock<std::mutex> lock(mutex);

                    if (timeoutMs <= 0)
                    {
                        // Wait indefinitely
                        condition.wait(lock, [this]() { return !queue.empty(); });
                    }
                    else
                    {
                        // Wait with timeout
                        if (!condition.wait_for(lock, std::chrono::milliseconds(timeoutMs), 
                                               [this]() { return !queue.empty(); }))
                        {
                            return false; // Timeout
                        }
                    }

                    if (queue.empty())
                    {
                        return false;
                    }

                    message = queue.front();
                    queue.pop();

                    LOGM_DEBUG(TAG, "Message dequeued: topic=%s, payload_size=%zu, queue_size=%zu", 
                              message.topic.c_str(), message.payload.size(), queue.size());
                    return true;
                }

                bool Queue::tryPop(QueuedMessage& message)
                {
                    std::lock_guard<std::mutex> lock(mutex);

                    if (queue.empty())
                    {
                        return false;
                    }

                    message = queue.front();
                    queue.pop();

                    LOGM_DEBUG(TAG, "Message dequeued (non-blocking): topic=%s, payload_size=%zu, queue_size=%zu", 
                              message.topic.c_str(), message.payload.size(), queue.size());
                    return true;
                }

                size_t Queue::size() const
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    return queue.size();
                }

                bool Queue::empty() const
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    return queue.empty();
                }

                void Queue::clear()
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    std::queue<QueuedMessage> empty;
                    queue.swap(empty);
                    recentHeartbeats.clear();
                    // Add trailing empty string to satisfy variadic macro expecting additional args
                    LOGM_INFO(TAG, "Queue cleared", "");
                }

                size_t Queue::getMaxSize() const
                {
                    return maxSize;
                }

                bool Queue::isHeartbeatTopic(const std::string& topic)
                {
                    // Common heartbeat topic patterns
                    static const std::vector<std::regex> heartbeatPatterns = {
                        std::regex(R"(.*/heartbeat$)", std::regex_constants::icase),
                        std::regex(R"(.*/ping$)", std::regex_constants::icase),
                        std::regex(R"(.*/alive$)", std::regex_constants::icase),
                        std::regex(R"(.*/status$)", std::regex_constants::icase),
                        std::regex(R"(.*heartbeat/.*)", std::regex_constants::icase),
                        std::regex(R"(.*ping/.*)", std::regex_constants::icase)
                    };

                    return std::any_of(heartbeatPatterns.begin(), heartbeatPatterns.end(),
                                       [&topic](const std::regex& pattern) {
                                           return std::regex_match(topic, pattern);
                                       });
                }

                bool Queue::isDuplicateHeartbeat(const std::string& topic)
                {
                    auto now = std::chrono::steady_clock::now();
                    
                    // Clean up old heartbeats periodically
                    auto timeSinceCleanup = std::chrono::duration_cast<std::chrono::seconds>(
                        now - lastHeartbeatCleanup);
                    if (timeSinceCleanup.count() >= heartbeatWindow)
                    {
                        cleanupHeartbeats();
                        lastHeartbeatCleanup = now;
                    }

                    std::string key = generateHeartbeatKey(topic);
                    
                    if (recentHeartbeats.find(key) != recentHeartbeats.end())
                    {
                        return true; // Duplicate
                    }

                    // Add to tracking set
                    recentHeartbeats.insert(key);
                    return false;
                }

                void Queue::cleanupHeartbeats()
                {
                    // For simplicity, clear all heartbeat entries
                    // In a more sophisticated implementation, we could track timestamps per entry
                    size_t oldSize = recentHeartbeats.size();
                    recentHeartbeats.clear();
                    
                    if (oldSize > 0)
                    {
                        LOGM_DEBUG(TAG, "Cleaned up %zu heartbeat tracking entries", oldSize);
                    }
                }

                std::string Queue::generateHeartbeatKey(const std::string& topic) const
                {
                    // Use topic as key - in more sophisticated scenarios we might 
                    // include payload hash or other identifiers
                    return topic;
                }

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws