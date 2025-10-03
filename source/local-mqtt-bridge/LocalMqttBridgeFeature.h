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
#include <memory>
#include <atomic>
#include <thread>
#include <functional>

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
                    // Expanded AWS control topic to trigger a sync of all up routes
                    std::string syncControlAwsTopic; // devices/${thingName}/control/local-mqtt-bridge/sync
                    // Optional: path to Sample Shadow input file for fallback routing
                    std::string sampleShadowInputFile;

                    // Core components
                    std::unique_ptr<RouteMatcher> routeMatcher;
                    std::unique_ptr<LoopGuard> loopGuard;
                    std::unique_ptr<Queue> localToAwsQueue;
                    std::unique_ptr<Queue> awsToLocalQueue;
                    std::unique_ptr<LocalClient> localClient;
                    // Track topics that need subscription (for up routes)
                    std::vector<std::string> upRouteLocalTopics;
                    // Guard for subscription list
                    std::mutex subscriptionMutex;
                    // Flag to know if initial subscription pass done after first connect
                    std::atomic<bool> initialLocalSubsDone{false};

                    // Threading
                    std::atomic<bool> running{false};
                    std::unique_ptr<std::thread> localToAwsThread;
                    std::unique_ptr<std::thread> awsToLocalThread;

                    // Metrics tracking
                    std::atomic<size_t> messagesForwarded{0};
                    std::atomic<size_t> messagesDropped{0};
                    std::chrono::steady_clock::time_point startTime;

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
                    // Subscribe to all up-route local topics (idempotent) if connected
                    void subscribeUpRouteTopicsIfConnected(bool forceResubscribe = false);

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

                    /**
                     * @brief Force-refresh all up-route local topics by resubscribing to trigger retained messages.
                     * This will cause the latest retained state on each local topic to be received and forwarded to AWS.
                     * @param reason Optional reason for logging (e.g., "initial-connect" or "aws-sync-request").
                     */
                    void requestSyncUpRoutes(const char* reason = nullptr);

                    /**
                     * @brief Attempt to load routes from an external JSON file if configured.
                     * @return true if routes were successfully loaded and applied; false otherwise
                     */
                    bool reloadRoutesFromExternalIfConfigured(const char* reason = nullptr);

                    /**
                     * @brief Replace current routes, rebuild matcher, update upRoute topics and (re)subscribe.
                     */
                    void applyRoutesAndResubscribe(const std::vector<PlainConfig::LocalMqttBridge::Route>& newRoutes,
                                                   bool forceResubscribe);
                };

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws

#endif // DEVICE_CLIENT_LOCALMQTTBRIDGEFEATURE_H
