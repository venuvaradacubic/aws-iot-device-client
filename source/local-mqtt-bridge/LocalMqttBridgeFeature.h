// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef DEVICE_CLIENT_LOCALMQTTBRIDGEFEATURE_H
#define DEVICE_CLIENT_LOCALMQTTBRIDGEFEATURE_H

#include "../Feature.h"
#include "../SharedCrtResourceManager.h"
#include "../ClientBaseNotifier.h"
#include "ConfigModel.h"
#include "RouteMatcher.h"
#include "PayloadTagger.h"
#include "LoopGuard.h"
#include "Queue.h"
#include "LocalClient.h"
#include "Metrics.h"
#include <memory>
#include <atomic>
#include <thread>

namespace Aws
{
    namespace Iot
    {
        namespace DeviceClient
        {
            namespace LocalMqttBridge
            {
                /**
                 * @brief Main feature class for Local MQTT Bridge functionality
                 * 
                 * This feature bridges selected topics between a local MQTT broker
                 * and AWS IoT Core with loop prevention and throttling.
                 */
                class LocalMqttBridgeFeature : public Feature
                {
                public:
                    static constexpr char NAME[] = "Local MQTT Bridge";
                    static constexpr char TAG[] = "LocalMqttBridge";

                    LocalMqttBridgeFeature();
                    ~LocalMqttBridgeFeature();

                    // Feature interface implementation
                    std::string getName() override;
                    int start() override;
                    int stop() override;

                    /**
                     * @brief Initialize the feature with configuration
                     * 
                     * @param manager Shared resource manager for AWS IoT connection
                     * @param notifier Client base notifier
                     * @param config Overall device client configuration
                     * @return SUCCESS (0) on success, non-zero on failure
                     */
                    int init(std::shared_ptr<SharedCrtResourceManager> manager,
                             std::shared_ptr<ClientBaseNotifier> notifier,
                             const PlainConfig& config);

                private:
                    std::shared_ptr<SharedCrtResourceManager> resourceManager;
                    std::shared_ptr<ClientBaseNotifier> baseNotifier;
                    LocalMqttBridgeConfig bridgeConfig;
                    std::string thingName; // Cache the thing name
                    
                    // Core components
                    std::unique_ptr<RouteMatcher> routeMatcher;
                    std::unique_ptr<LoopGuard> loopGuard;
                    std::unique_ptr<Queue> localToAwsQueue;
                    std::unique_ptr<Queue> awsToLocalQueue;
                    std::unique_ptr<LocalClient> localClient;
                    
                    // Threading
                    std::atomic<bool> running{false};
                    std::unique_ptr<std::thread> localToAwsThread;
                    std::unique_ptr<std::thread> awsToLocalThread;
                    
                    // Legacy simple counters (retained for backward logging) & start time
                    std::atomic<size_t> messagesForwarded{0};
                    std::atomic<size_t> messagesDropped{0};
                    std::chrono::steady_clock::time_point startTime;

                    // Enhanced metrics subsystem
                    std::unique_ptr<Metrics> metrics;
                    std::unique_ptr<std::thread> metricsThread;

                    // Offline buffer for AWS disconnect scenario
                    std::deque<QueuedMessage> offlineBuffer;
                    std::mutex offlineMutex;

                    /**
                     * @brief Load configuration from PlainConfig
                     * @param config The device client configuration
                     */
                    void loadFromConfig(const PlainConfig& config);

                    /**
                     * @brief Validate the bridge configuration
                     * @return true if configuration is valid
                     */
                    bool validateConfig() const;

                    /**
                     * @brief Initialize all bridge components
                     * @return true if initialization succeeded
                     */
                    bool initializeComponents();

                    /**
                     * @brief Setup subscriptions and connections
                     * @return true if setup succeeded
                     */
                    bool setupConnections();

                    /**
                     * @brief Cleanup all resources and stop threads
                     */
                    void cleanup();

                    /**
                     * @brief Thread function for forwarding messages from local to AWS
                     */
                    void localToAwsThreadFunction();

                    /**
                     * @brief Thread function for forwarding messages from AWS to local
                     */
                    void awsToLocalThreadFunction();
                    void metricsThreadFunction();

                    /**
                     * @brief Handle message received from local broker
                     */
                    void handleLocalMessage(const std::string& topic, const void* payload, int payloadLen);

                    /**
                     * @brief Handle message received from AWS IoT
                     */
                    void handleAwsMessage(const std::string& topic, const std::string& payload);

                    /**
                     * @brief Get thing name from configuration or connection
                     */
                    std::string getThingName() const;

                    /**
                     * @brief Expand topic template with thing name
                     */
                    std::string expandTopic(const std::string& topicTemplate, 
                                           const std::string& thingName) const;

                    bool isAwsConnected() const;
                    void drainOffline();
                    void enqueueOffline(const QueuedMessage &msg);
                };

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws

#endif // DEVICE_CLIENT_LOCALMQTTBRIDGEFEATURE_H