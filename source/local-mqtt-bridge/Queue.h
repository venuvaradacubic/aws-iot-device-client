// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <unordered_set>

namespace Aws
{
    namespace Iot
    {
        namespace DeviceClient
        {
            namespace LocalMqttBridge
            {

                /**
                 * \brief Message structure for queue operations
                 */
                struct QueuedMessage
                {
                    std::string topic;
                    std::string payload;
                    std::chrono::steady_clock::time_point timestamp;
                    int retryCount = 0;
                    bool isFromLocal = true;  // true if from local broker, false if from AWS IoT
                    
                    QueuedMessage() = default;
                    
                    QueuedMessage(const std::string& topic, const std::string& payload, bool isFromLocal = true)
                        : topic(topic), payload(payload), timestamp(std::chrono::steady_clock::now()), 
                          isFromLocal(isFromLocal) {}
                };

                /**
                 * \brief Thread-safe message queue with deduplication and heartbeat filtering
                 * 
                 * This queue provides thread-safe buffering of MQTT messages with:
                 * - Automatic heartbeat message deduplication
                 * - Size-based queue management
                 * - Timeout-based blocking operations
                 * - Message retry tracking
                 */
                class Queue
                {
                    static constexpr char TAG[] = "Queue";
                    
                public:
                    /**
                     * \brief Construct a new Queue
                     * 
                     * \param maxSize Maximum number of messages to buffer
                     * \param heartbeatWindow Time window in seconds for heartbeat deduplication
                     */
                    explicit Queue(size_t maxSize = 1000, int heartbeatWindow = 5);
                    
                    /**
                     * \brief Destructor
                     */
                    ~Queue();

                    /**
                     * \brief Push a message to the queue
                     * 
                     * \param message Message to queue
                     * \return true if message was queued, false if dropped (queue full or duplicate heartbeat)
                     */
                    bool push(const QueuedMessage& message);
                    
                    /**
                     * \brief Push a message to the queue
                     * 
                     * \param topic MQTT topic
                     * \param payload Message payload
                     * \param isFromLocal true if from local broker, false if from AWS IoT
                     * \return true if message was queued, false if dropped
                     */
                    bool push(const std::string& topic, const std::string& payload, bool isFromLocal = true);

                    /**
                     * \brief Pop a message from the queue (blocking with timeout)
                     * 
                     * \param message Output parameter for the popped message
                     * \param timeoutMs Timeout in milliseconds (0 = no timeout)
                     * \return true if message was retrieved, false if timeout or queue empty
                     */
                    bool pop(QueuedMessage& message, int timeoutMs = 0);

                    /**
                     * \brief Try to pop a message without blocking
                     * 
                     * \param message Output parameter for the popped message
                     * \return true if message was retrieved, false if queue empty
                     */
                    bool tryPop(QueuedMessage& message);

                    /**
                     * \brief Get current queue size
                     * 
                     * \return Number of messages in queue
                     */
                    size_t size() const;

                    /**
                     * \brief Check if queue is empty
                     * 
                     * \return true if queue is empty
                     */
                    bool empty() const;

                    /**
                     * \brief Clear all messages from queue
                     */
                    void clear();

                    /**
                     * \brief Get maximum queue size
                     * 
                     * \return Maximum queue capacity
                     */
                    size_t getMaxSize() const;

                    /**
                     * \brief Check if topic appears to be a heartbeat topic
                     * 
                     * \param topic MQTT topic to check
                     * \return true if topic matches common heartbeat patterns
                     */
                    static bool isHeartbeatTopic(const std::string& topic);

                private:
                    std::queue<QueuedMessage> queue;
                    mutable std::mutex mutex;
                    std::condition_variable condition;
                    size_t maxSize;
                    int heartbeatWindow;
                    
                    // Heartbeat deduplication tracking
                    std::unordered_set<std::string> recentHeartbeats;
                    std::chrono::steady_clock::time_point lastHeartbeatCleanup;
                    
                    /**
                     * \brief Check if a heartbeat message is a duplicate
                     * 
                     * \param topic MQTT topic
                     * \return true if this heartbeat was seen recently
                     */
                    bool isDuplicateHeartbeat(const std::string& topic);
                    
                    /**
                     * \brief Clean up old heartbeat entries
                     */
                    void cleanupHeartbeats();
                    
                    /**
                     * \brief Generate a unique key for heartbeat tracking
                     * 
                     * \param topic MQTT topic
                     * \return Unique key string
                     */
                    std::string generateHeartbeatKey(const std::string& topic) const;
                };

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws