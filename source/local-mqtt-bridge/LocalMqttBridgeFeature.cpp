// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LocalMqttBridgeFeature.h"
#include "ConfigModel.h"
#include "RouteMatcher.h"
#include "PayloadTagger.h"
#include "LoopGuard.h"
#include "Queue.h"
#include "LocalClient.h"
#include "../logging/LoggerFactory.h"
#include <aws/crt/mqtt/MqttConnection.h>
#include <aws/crt/JsonObject.h>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>

using namespace std;
using namespace Aws::Iot::DeviceClient::LocalMqttBridge;
using namespace Aws::Iot::DeviceClient::Logging;
using namespace Aws::Iot::DeviceClient;

namespace Aws
{
    namespace Iot
    {
        namespace DeviceClient
        {
            namespace LocalMqttBridge
            {
                constexpr char LocalMqttBridgeFeature::NAME[];
                constexpr char LocalMqttBridgeFeature::TAG[];

                LocalMqttBridgeFeature::LocalMqttBridgeFeature()
                {
                    // Avoid std::make_unique to keep compatibility with C++11 (project may compile with -std=c++11)
                    routeMatcher = std::unique_ptr<RouteMatcher>(new RouteMatcher());
                }

                LocalMqttBridgeFeature::~LocalMqttBridgeFeature()
                {
                    if (running)
                    {
                        stop();
                    }
                    cleanup();
                }

                std::string LocalMqttBridgeFeature::getName()
                {
                    return NAME;
                }

                int LocalMqttBridgeFeature::init(std::shared_ptr<SharedCrtResourceManager> manager,
                                                std::shared_ptr<ClientBaseNotifier> notifier,
                                                const PlainConfig& config)
                {
                    resourceManager = manager;
                    baseNotifier = notifier;

                    // Extract thing name from config
                    if (config.thingName.has_value() && !config.thingName->empty())
                    {
                        thingName = *config.thingName;
                        LOGM_INFO(TAG, "Using thing name: %s", thingName.c_str());
                    }
                    else
                    {
                        thingName = "unknown-device";
                        LOGM_WARN(TAG, "No thing name configured, using default: %s", thingName.c_str());
                    }

                    loadFromConfig(config);
                    // cache sample shadow input file for fallback logic if configured
                    if (config.sampleShadow.shadowInputFile.has_value())
                    {
                        sampleShadowInputFile = config.sampleShadow.shadowInputFile.value();
                    }

                    if (!validateConfig())
                    {
                        LOGM_ERROR(TAG, "%s", "Invalid Local MQTT Bridge configuration");
                        return -1;
                    }

                    if (!initializeComponents())
                    {
                        LOGM_ERROR(TAG, "%s", "Failed to initialize Local MQTT Bridge components");
                        return -1;
                    }

                    LOGM_INFO(TAG, "%s", "Local MQTT Bridge initialized successfully");
                    return Feature::SUCCESS;
                }

                int LocalMqttBridgeFeature::start()
                {
                    if (running)
                    {
                        LOGM_WARN(TAG, "%s", "Local MQTT Bridge is already running");
                        return Feature::SUCCESS;
                    }

                    if (!bridgeConfig.enabled)
                    {
                        LOGM_INFO(TAG, "%s", "Local MQTT Bridge is disabled in configuration");
                        return Feature::SUCCESS;
                    }

                    LOGM_INFO(TAG, "Starting Local MQTT Bridge (routes from config: %zu, external file will be loaded immediately)",
                              bridgeConfig.routes.size());

                    // Mark as running and set start time
                    running = true;
                    startTime = std::chrono::steady_clock::now();

                    // Start processing threads first (they will wait for connections)
                    localToAwsThread.reset(new std::thread(&LocalMqttBridgeFeature::localToAwsThreadFunction, this));
                    awsToLocalThread.reset(new std::thread(&LocalMqttBridgeFeature::awsToLocalThreadFunction, this));

                    // Start file monitoring thread if routes file is configured
                    if (bridgeConfig.routesFile.has_value() && !bridgeConfig.routesFile->empty())
                    {
                        fileMonitorThread.reset(new std::thread(&LocalMqttBridgeFeature::fileMonitorThreadFunction, this));
                        LOGM_INFO(TAG, "%s", "File monitoring thread started");
                    }

                    // Setup connections immediately (no delay needed for static routes file)
                    LOGM_INFO(TAG, "%s", "Setting up connections and loading routes from static file...");
                    if (!setupConnections())
                    {
                        LOGM_ERROR(TAG, "%s", "Failed to setup connections");
                        running.store(false);
                        return -1;
                    }

                    LOGM_INFO(TAG, "%s", "Local MQTT Bridge started successfully");
                    return Feature::SUCCESS;
                }

                int LocalMqttBridgeFeature::stop()
                {
                    if (!running)
                    {
                        return Feature::SUCCESS;
                    }

                    LOGM_INFO(TAG, "%s", "Stopping Local MQTT Bridge");
                    running = false;

                    // Stop local client
                    if (localClient)
                    {
                        localClient->stop();
                    }

                    // Wait for threads to finish
                    if (localToAwsThread && localToAwsThread->joinable())
                    {
                        localToAwsThread->join();
                        localToAwsThread.reset();
                    }

                    if (awsToLocalThread && awsToLocalThread->joinable())
                    {
                        awsToLocalThread->join();
                        awsToLocalThread.reset();
                    }

                    if (fileMonitorThread && fileMonitorThread->joinable())
                    {
                        fileMonitorThread->join();
                        fileMonitorThread.reset();
                    }

                    // Print final statistics
                    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - startTime);

                    LOGM_INFO(TAG, "Local MQTT Bridge stopped - Uptime: %ld seconds, "
                             "Messages forwarded: %zu, Messages dropped: %zu",
                             uptime.count(), messagesForwarded.load(), messagesDropped.load());

                    return Feature::SUCCESS;
                }

                void LocalMqttBridgeFeature::loadFromConfig(const PlainConfig& config)
                {
                    bridgeConfig = config.localMqttBridge;
                    if (bridgeConfig.routesFile.has_value() && !bridgeConfig.routesFile->empty())
                    {
                        LOGM_INFO(TAG, "Loaded configuration - enabled: %s, routes in config: %zu, external routes file: %s",
                                  bridgeConfig.enabled ? "true" : "false",
                                  bridgeConfig.routes.size(),
                                  bridgeConfig.routesFile->c_str());
                    }
                    else
                    {
                        LOGM_INFO(TAG, "Loaded configuration - enabled: %s, routes: %zu",
                                  bridgeConfig.enabled ? "true" : "false",
                                  bridgeConfig.routes.size());
                    }
                }

                bool LocalMqttBridgeFeature::validateConfig() const
                {
                    if (!bridgeConfig.enabled)
                    {
                        return true; // Valid to be disabled
                    }

                    // Validate local broker configuration
                    if (bridgeConfig.local.host.empty())
                    {
                        LOGM_ERROR(TAG, "%s", "Local broker host cannot be empty");
                        return false;
                    }

                    if (bridgeConfig.local.port <= 0 || bridgeConfig.local.port > 65535)
                    {
                        LOGM_ERROR(TAG, "%s", "Local broker port must be between 1 and 65535");
                        return false;
                    }

                    // Validate routes
                    for (const auto& route : bridgeConfig.routes)
                    {
                        if (!isValidDirection(route.direction))
                        {
                            LOGM_ERROR(TAG, "Invalid route direction: %s", route.direction.c_str());
                            return false;
                        }

                        if (!isValidQoS(route.qos))
                        {
                            LOGM_ERROR(TAG, "Invalid route QoS: %d", route.qos);
                            return false;
                        }

                        if (route.direction == "up" && (route.localTopic.empty() || route.awsTopic.empty()))
                        {
                            LOGM_ERROR(TAG, "%s", "Up route missing required topics");
                            return false;
                        }

                        if (route.direction == "down" && (route.awsTopic.empty() || route.localTopicTemplate.empty()))
                        {
                            LOGM_ERROR(TAG, "%s", "Down route missing required topic template");
                            return false;
                        }
                    }

                    return true;
                }

                bool LocalMqttBridgeFeature::initializeComponents()
                {
                    try
                    {
                        // Initialize loop guard
                        loopGuard.reset(new LoopGuard(
                            bridgeConfig.loopGuard.ttlSeconds,
                            bridgeConfig.loopGuard.maxEntries
                        ));

                        // Initialize queues
                        localToAwsQueue.reset(new Queue(
                            bridgeConfig.queue.maxInMemory,
                            5 // heartbeat deduplication window in seconds
                        ));
                        awsToLocalQueue.reset(new Queue(
                            bridgeConfig.queue.maxInMemory,
                            5 // heartbeat deduplication window in seconds
                        ));

                        // Initialize local MQTT client
                        localClient.reset(new LocalClient(
                            "aws-iot-device-client-bridge"
                        ));

                        // Set up local client callbacks
                        localClient->setMessageCallback(
                            std::bind(&LocalMqttBridgeFeature::handleLocalMessage, this,
                                    std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
                        );

                        LOGM_INFO(TAG, "%s", "All bridge components initialized successfully");
                        return true;
                    }
                    catch (const std::exception& e)
                    {
                        LOGM_ERROR(TAG, "Failed to initialize components: %s", e.what());
                        return false;
                    }
                }

                bool LocalMqttBridgeFeature::setupConnections()
                {
                    // Ensure routeMatcher is valid
                    if (!routeMatcher)
                    {
                        LOGM_ERROR(TAG, "%s", "RouteMatcher not initialized");
                        return false;
                    }

                    // Prepare route matcher and list of up-route topics (expanded now for fixed placeholders)
                    std::string tn = getThingName();
                    // Try to load external routes if configured (will fallback to input file internally if output is empty)
                    // Note: reloadRoutesFromExternalIfConfigured will rebuild matcher and resubscribe as needed when it applies.
                    bool reloaded = reloadRoutesFromExternalIfConfigured("startup");
                    if (!reloaded)
                    {
                        LOGM_INFO(TAG, "No external routes file configured or loaded, using config routes (%zu)",
                                  bridgeConfig.routes.size());
                        // No external reload occurred; build matcher and topic list from existing config
                        routeMatcher->addRoutes(bridgeConfig.routes, tn);
                        std::lock_guard<std::mutex> lock(subscriptionMutex);
                        upRouteLocalTopics.clear();
                        for (const auto &route : bridgeConfig.routes)
                        {
                            if (route.direction == "up" && !route.localTopic.empty())
                            {
                                upRouteLocalTopics.push_back(expandTopic(route.localTopic, tn));
                            }
                        }
                    }
                    // Prepare sync control AWS topic
                    syncControlAwsTopic = "devices/${thingName}/control/local-mqtt-bridge/sync";
                    syncControlAwsTopic = expandTopic(syncControlAwsTopic, tn);
                    // upRouteLocalTopics will be prepared by applyRoutesAndResubscribe during reload

                    // Ensure local client is valid
                    if (!localClient)
                    {
                        LOGM_ERROR(TAG, "%s", "Local MQTT client not initialized");
                        return false;
                    }

                    // Set connection callback to (re)subscribe after connect events
                    localClient->setConnectionCallback([this](bool connected, int /*reason*/){
                        if (connected)
                        {
                            // On first successful local connect, trigger a one-time explicit sync to pull retained messages
                            // (which will force resubscribe). On reconnects, perform a normal subscribe pass.
                            bool first = !initialLocalSubsDone.load();
                            if (first)
                            {
                                initialLocalSubsDone.store(true);
                                requestSyncUpRoutes("initial-connect");
                            }
                            else
                            {
                                subscribeUpRouteTopicsIfConnected(false);
                            }
                        }
                        else
                        {
                            // Clear local subscription tracking so we re-subscribe after reconnect
                            std::lock_guard<std::mutex> lock(subscriptionMutex);
                            subscribedLocalTopics.clear();
                        }
                    });

                    localClient->start();
                    bool localConnectInitiated = localClient->connect(
                        bridgeConfig.local.host,
                        bridgeConfig.local.port,
                        60,
                        bridgeConfig.local.username,
                        bridgeConfig.local.password);
                    if (!localConnectInitiated)
                    {
                        LOGM_ERROR(TAG, "%s", "Failed to initiate connection to local MQTT broker");
                        return false;
                    }
                    // Subscriptions will occur asynchronously when onConnect callback fires
                    auto connection = resourceManager->getConnection();
                    if (!connection)
                    {
                        LOGM_ERROR(TAG, "%s", "AWS IoT connection unavailable during bridge setup");
                        return false;
                    }
                    // AWS down routes
                    for (const auto &route : bridgeConfig.routes)
                    {
                        if (route.direction == "down" && !route.awsTopic.empty())
                        {
                            std::string expandedAws = expandTopic(route.awsTopic, tn);
                            Aws::Crt::Mqtt::OnMessageReceivedHandler handler =
                                [this](Aws::Crt::Mqtt::MqttConnection &,
                                       const Aws::Crt::String &receivedOnTopic,
                                       const Aws::Crt::ByteBuf &payload,
                                       bool, Aws::Crt::Mqtt::QOS, bool)
                                {
                                    this->handleAwsMessage(receivedOnTopic.c_str(),
                                                          std::string(reinterpret_cast<const char *>(payload.buffer), payload.len));
                                };
                            auto onSubAck = [expandedAws](const Aws::Crt::Mqtt::MqttConnection &,
                                                          uint16_t, const Aws::Crt::String &,
                                                          Aws::Crt::Mqtt::QOS, int errorCode)
                            {
                                if (errorCode == 0)
                                {
                                    LOGM_DEBUG(LocalMqttBridgeFeature::TAG, "Subscribed to AWS IoT topic: %s", expandedAws.c_str());
                                }
                                else
                                {
                                    LOGM_ERROR(LocalMqttBridgeFeature::TAG, "Failed to subscribe to AWS IoT topic %s, error: %d", expandedAws.c_str(), errorCode);
                                }
                            };
                            uint16_t packetId = connection->Subscribe(expandedAws.c_str(),
                                                                     static_cast<Aws::Crt::Mqtt::QOS>(route.qos),
                                                                     std::move(handler), std::move(onSubAck));
                            if (packetId == 0)
                            {
                                LOGM_ERROR(TAG, "Failed to initiate subscription to AWS IoT topic: %s", expandedAws.c_str());
                            }
                        }
                    }
                    // Subscribe to sync control topic
                    {
                        Aws::Crt::Mqtt::OnMessageReceivedHandler handler =
                            [this](Aws::Crt::Mqtt::MqttConnection &,
                                   const Aws::Crt::String &receivedOnTopic,
                                   const Aws::Crt::ByteBuf &payload,
                                   bool, Aws::Crt::Mqtt::QOS, bool)
                            {
                                this->handleAwsMessage(receivedOnTopic.c_str(),
                                                      std::string(reinterpret_cast<const char *>(payload.buffer), payload.len));
                            };
                        auto onSubAck = [this](const Aws::Crt::Mqtt::MqttConnection &,
                                                uint16_t, const Aws::Crt::String &,
                                                Aws::Crt::Mqtt::QOS, int errorCode)
                        {
                            if (errorCode == 0)
                            {
                                LOGM_DEBUG(LocalMqttBridgeFeature::TAG, "Subscribed to AWS sync control topic: %s", this->syncControlAwsTopic.c_str());
                            }
                            else
                            {
                                LOGM_ERROR(LocalMqttBridgeFeature::TAG, "Failed to subscribe to AWS sync control topic %s, error: %d", this->syncControlAwsTopic.c_str(), errorCode);
                            }
                        };
                        uint16_t packetId = connection->Subscribe(syncControlAwsTopic.c_str(),
                                                                 Aws::Crt::Mqtt::QOS::AWS_MQTT_QOS_AT_LEAST_ONCE,
                                                                 std::move(handler), std::move(onSubAck));
                        if (packetId == 0)
                        {
                            LOGM_ERROR(TAG, "Failed to initiate subscription to AWS sync control topic: %s", syncControlAwsTopic.c_str());
                        }
                    }
                    return true;
                }

                void LocalMqttBridgeFeature::subscribeUpRouteTopicsIfConnected(bool forceResubscribe)
                {
                    if (!localClient || !localClient->isConnected())
                    {
                        return;
                    }
                    std::lock_guard<std::mutex> lock(subscriptionMutex);
                    // Build a set of desired topics for quick diff
                    std::unordered_set<std::string> desired(upRouteLocalTopics.begin(), upRouteLocalTopics.end());

                    // Unsubscribe topics that are no longer desired or when forceResubscribe is requested
                    for (auto it = subscribedLocalTopics.begin(); it != subscribedLocalTopics.end(); )
                    {
                        const std::string &t = *it;
                        bool shouldUnsub = (desired.find(t) == desired.end()) || forceResubscribe;
                        if (shouldUnsub)
                        {
                            localClient->unsubscribe(t);
                            LOGM_DEBUG(TAG, "Unsubscribed local topic: %s", t.c_str());
                            it = subscribedLocalTopics.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }

                    // Subscribe topics that are desired but not currently subscribed
                    for (const auto &topic : upRouteLocalTopics)
                    {
                        if (subscribedLocalTopics.find(topic) == subscribedLocalTopics.end())
                        {
                            if (!localClient->subscribe(topic, 0))
                            {
                                LOGM_WARN(TAG, "Subscribe failed for local topic: %s", topic.c_str());
                            }
                            else
                            {
                                subscribedLocalTopics.insert(topic);
                                LOGM_DEBUG(TAG, "Subscribed local topic: %s", topic.c_str());
                            }
                        }
                    }
                }

                void LocalMqttBridgeFeature::cleanup()
                {
                    // Reset all components
                    localClient.reset();
                    awsToLocalQueue.reset();
                    localToAwsQueue.reset();
                    loopGuard.reset();
                }

                void LocalMqttBridgeFeature::localToAwsThreadFunction()
                {
                    LOGM_INFO(TAG, "%s", "Local-to-AWS message forwarding thread started");

                    while (running.load())
                    {
                        QueuedMessage message;
                        if (localToAwsQueue->pop(message, 1000)) // 1 second timeout
                        {
                            // Check loop guard
                            if (loopGuard->shouldDrop("up", message.topic, message.payload))
                            {
                                messagesDropped++;
                                continue;
                            }

                            // Find matching route (with variable capture for '+')
                            auto matchResult = routeMatcher->matchUpWithVariables(message.topic);
                            const Route* matchedRoute = matchResult ? matchResult->route : nullptr;
                            if (matchedRoute)
                            {
                                // Throttling logic: if route has throttleSeconds, decide send/defer
                                bool throttled = false;
                                std::string effectivePayload = message.payload;
                                if (matchedRoute->throttleSeconds > 0)
                                {
                                    auto now = std::chrono::steady_clock::now();
                                    std::string throttleKey = matchedRoute->awsTopic; // use aws topic template as key
                                    {
                                        static std::mutex throttleMutex; // local static to avoid adding member until metrics phase
                                        static std::unordered_map<std::string, std::pair<std::chrono::steady_clock::time_point, std::string>> throttleMap; // lastSent, cachedPayload
                                        std::lock_guard<std::mutex> lock(throttleMutex);
                                        auto &entry = throttleMap[throttleKey];
                                        auto last = entry.first;
                                        if (last.time_since_epoch().count() != 0)
                                        {
                                            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last).count();
                                            if (elapsed < matchedRoute->throttleSeconds)
                                            {
                                                // cache latest payload and skip send
                                                entry.second = message.payload;
                                                throttled = true;
                                            }
                                            else
                                            {
                                                // send (using newest cached if present)
                                                if (!entry.second.empty())
                                                {
                                                    effectivePayload = entry.second;
                                                    entry.second.clear();
                                                }
                                                entry.first = now;
                                            }
                                        }
                                        else
                                        {
                                            // first send
                                            entry.first = now;
                                            entry.second.clear();
                                        }
                                    }
                                }

                                if (throttled)
                                {
                                    continue; // skip publish this cycle
                                }
                                // Do not mutate payload; rely on LoopGuard for loop prevention
                                std::string taggedPayload = effectivePayload;

                                // Expand AWS topic including ${matchN} variables
                                std::string awsTopic = matchedRoute->awsTopic;
                                // Replace thingName
                                awsTopic = expandTopic(awsTopic, getThingName());
                                if (matchResult)
                                {
                                    for (const auto &kv : matchResult->variables)
                                    {
                                        std::string placeholder = "${" + kv.first + "}";
                                        size_t pos = 0;
                                        while ((pos = awsTopic.find(placeholder, pos)) != std::string::npos)
                                        {
                                            awsTopic.replace(pos, placeholder.length(), kv.second);
                                            pos += kv.second.length();
                                        }
                                    }
                                }

                                // Publish to AWS IoT Core using resourceManager
                                auto connection = resourceManager->getConnection();
                                if (connection)
                                {
                                    Aws::Crt::ByteBuf payloadBuf = Aws::Crt::ByteBufFromArray(
                                        reinterpret_cast<const uint8_t*>(taggedPayload.data()),
                                        taggedPayload.size()
                                    );

                                    auto onPubAck = [awsTopic](Aws::Crt::Mqtt::MqttConnection&, uint16_t, int errorCode) {
                                        if (errorCode == 0)
                                        {
                                            LOGM_DEBUG(TAG, "Successfully published to AWS IoT topic: %s",
                                                      awsTopic.c_str());
                                        }
                                        else
                                        {
                                            LOGM_ERROR(TAG, "Failed to publish to AWS IoT topic %s, error: %d",
                                                      awsTopic.c_str(), errorCode);
                                        }
                                    };

                                    int desiredQos = matchedRoute->qos;
                                    if (desiredQos == 2)
                                    {
                                        LOGM_WARN(TAG, "QoS 2 requested for topic %s, downgrading to QoS 1", awsTopic.c_str());
                                        desiredQos = 1;
                                    }
                                    uint16_t packetId = connection->Publish(awsTopic.c_str(),
                                                                           static_cast<Aws::Crt::Mqtt::QOS>(desiredQos),
                                                                           false, // retain
                                                                           payloadBuf,
                                                                           onPubAck);

                                    if (packetId != 0)
                                    {
                                        messagesForwarded++;
                                        LOGM_DEBUG(TAG, "Forwarded message from local to AWS: %s -> %s",
                                                  message.topic.c_str(), awsTopic.c_str());
                                    }
                                    else
                                    {
                                        LOGM_ERROR(TAG, "Failed to initiate publish to AWS IoT topic: %s",
                                                  awsTopic.c_str());
                                        messagesDropped++;
                                    }
                                }
                                else
                                {
                                    LOGM_ERROR(TAG, "No AWS IoT connection available for publishing to: %s",
                                              awsTopic.c_str());
                                    messagesDropped++;
                                }
                            }
                            else
                            {
                                LOGM_DEBUG(TAG, "No route found for local topic: %s", message.topic.c_str());
                                messagesDropped++;
                            }
                        }
                    }

                    LOGM_INFO(TAG, "%s", "Local-to-AWS message forwarding thread stopped");
                }

                void LocalMqttBridgeFeature::awsToLocalThreadFunction()
                {
                    LOGM_INFO(TAG, "%s", "AWS-to-local message forwarding thread started");

                    while (running.load())
                    {
                        QueuedMessage message;
                        if (awsToLocalQueue->pop(message, 1000)) // 1 second timeout
                        {
                            // Check loop guard
                            if (loopGuard->shouldDrop("down", message.topic, message.payload))
                            {
                                messagesDropped++;
                                continue;
                            }

                            // Find matching route
                            auto matchResult = routeMatcher->matchDown(message.topic);
                            if (matchResult && matchResult->route)
                            {
                                // Generate local topic from template and variables
                                std::string localTopic = routeMatcher->generateLocalTopic(
                                    matchResult->route, matchResult->variables
                                );

                                // Remove bridge tags from payload (PayloadTagger doesn't have untagMessage)
                                std::string cleanPayload = message.payload;

                                // Publish to local broker
                                if (localClient && localClient->isConnected())
                                {
                                    bool published = localClient->publish(
                                        localTopic,
                                        cleanPayload,
                                        matchResult->route->qos
                                    );

                                    if (published)
                                    {
                                        messagesForwarded++;
                                        LOGM_DEBUG(TAG, "Forwarded message from AWS to local: %s -> %s",
                                                  message.topic.c_str(), localTopic.c_str());
                                    }
                                    else
                                    {
                                        LOGM_WARN(TAG, "Failed to publish to local broker: %s",
                                                 localTopic.c_str());
                                        messagesDropped++;
                                    }
                                }
                                else
                                {
                                    LOGM_WARN(TAG, "%s", "Local client not connected, dropping message");
                                    messagesDropped++;
                                }
                            }
                            else
                            {
                                LOGM_DEBUG(TAG, "No route found for AWS topic: %s", message.topic.c_str());
                                messagesDropped++;
                            }
                        }
                    }

                    LOGM_INFO(TAG, "%s", "AWS-to-local message forwarding thread stopped");
                }

                void LocalMqttBridgeFeature::fileMonitorThreadFunction()
                {
                    LOGM_INFO(TAG, "%s", "File monitoring thread started");

                    if (!bridgeConfig.routesFile.has_value() || bridgeConfig.routesFile->empty())
                    {
                        LOGM_WARN(TAG, "%s", "No routes file configured for monitoring");
                        return;
                    }

                    const std::string path = *bridgeConfig.routesFile;
                    const int checkIntervalSeconds = 60;

                    // Initialize last modification time
                    {
                        std::lock_guard<std::mutex> lock(fileMonitorMutex);
                        struct stat fileStat;
                        if (stat(path.c_str(), &fileStat) == 0)
                        {
                            lastRouteFileModTime = fileStat.st_mtime;
                            LOGM_INFO(TAG, "Initial routes file mtime: %ld", (long)lastRouteFileModTime);
                        }
                        else
                        {
                            LOGM_WARN(TAG, "Could not stat routes file for monitoring: %s", path.c_str());
                        }
                    }

                    while (running.load())
                    {
                        // Sleep for check interval
                        for (int i = 0; i < checkIntervalSeconds && running.load(); ++i)
                        {
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                        }

                        if (!running.load())
                        {
                            break;
                        }

                        // Check if file has been modified
                        struct stat fileStat;
                        if (stat(path.c_str(), &fileStat) != 0)
                        {
                            LOGM_WARN(TAG, "Could not stat routes file: %s (errno: %d)", path.c_str(), errno);
                            continue;
                        }

                        bool fileChanged = false;
                        {
                            std::lock_guard<std::mutex> lock(fileMonitorMutex);
                            if (fileStat.st_mtime != lastRouteFileModTime)
                            {
                                LOGM_INFO(TAG, "Routes file changed (mtime: %ld -> %ld), reloading...",
                                         (long)lastRouteFileModTime, (long)fileStat.st_mtime);
                                lastRouteFileModTime = fileStat.st_mtime;
                                fileChanged = true;
                            }
                        }

                        if (fileChanged)
                        {
                            // Reload routes from file
                            if (reloadRoutesFromExternalIfConfigured("file-changed"))
                            {
                                LOGM_INFO(TAG, "%s", "Successfully reloaded routes after file change");
                            }
                            else
                            {
                                LOGM_ERROR(TAG, "%s", "Failed to reload routes after file change");
                            }
                        }
                    }

                    LOGM_INFO(TAG, "%s", "File monitoring thread stopped");
                }

                void LocalMqttBridgeFeature::handleLocalMessage(const std::string& topic,
                                                               const void* payload, int payloadLen)
                {
                    // Process even if not fully running to capture retained messages immediately after subscription

                    // Create string from payload with safety checks
                    std::string payloadStr;
                    if (payload != nullptr && payloadLen > 0)
                    {
                        payloadStr = std::string(static_cast<const char*>(payload), payloadLen);
                    }

                    // We no longer use payload tagging; LoopGuard handles loop prevention.

                    // Queue message for processing
                    if (!localToAwsQueue->push(topic, payloadStr, true))
                    {
                        if (Queue::isHeartbeatTopic(topic))
                        {
                            // Likely dropped as duplicate within heartbeat window; that's expected
                            LOGM_DEBUG(TAG, "Dropped duplicate heartbeat from local broker: %s", topic.c_str());
                        }
                        else
                        {
                            LOGM_WARN(TAG, "Failed to queue message from local broker: %s", topic.c_str());
                        }
                        messagesDropped++;
                    }
                }

                void LocalMqttBridgeFeature::handleAwsMessage(const std::string& topic,
                                                             const std::string& payload)
                {
                    // Always allow control topic handling
                    if (topic == syncControlAwsTopic)
                    {
                        // Always honor on-demand sync requests from AWS; do not debounce
                        LOGM_INFO(TAG, "Received sync request from AWS: %s", topic.c_str());
                        requestSyncUpRoutes("aws-sync-request");
                        return;
                    }
                    if (!running.load())
                    {
                        return;
                    }

                    // Queue message for processing
                    if (!awsToLocalQueue->push(topic, payload, false))
                    {
                        LOGM_WARN(TAG, "Failed to queue message from AWS IoT: %s", topic.c_str());
                        messagesDropped++;
                    }
                }

                std::string LocalMqttBridgeFeature::getThingName() const
                {
                    return thingName;
                }

                std::string LocalMqttBridgeFeature::expandTopic(const std::string& topicTemplate,
                                                               const std::string& thingName) const
                {
                    std::string result = topicTemplate;

                    // Replace ${thingName} placeholder
                    size_t pos = 0;
                    while ((pos = result.find("${thingName}", pos)) != std::string::npos)
                    {
                        result.replace(pos, 12, thingName);
                        pos += thingName.length();
                    }

                    return result;
                }

                void LocalMqttBridgeFeature::requestSyncUpRoutes(const char* reason)
                {
                    if (reason)
                    {
                        LOGM_INFO(TAG, "Syncing up-route retained state (%s)", reason);
                    }
                    else
                    {
                        LOGM_INFO(TAG, "%s", "Syncing up-route retained state");
                    }
                    // Re-subscribe to up-route topics to trigger retained message delivery
                    // For initial-connect, we skip reloading routes to avoid duplicate load/apply noise
                    if (!(reason && std::string(reason) == "initial-connect"))
                    {
                        reloadRoutesFromExternalIfConfigured("sync");
                    }
                    subscribeUpRouteTopicsIfConnected(true);
                }

                bool LocalMqttBridgeFeature::reloadRoutesFromExternalIfConfigured(const char* reason)
                {
                    if (!bridgeConfig.routesFile.has_value() || bridgeConfig.routesFile->empty())
                    {
                        LOGM_DEBUG(TAG, "%s", "No external routes file configured");
                        return false;
                    }

                    const std::string path = *bridgeConfig.routesFile;
                    LOGM_INFO(TAG, "Loading routes from: %s (reason: %s)", path.c_str(), reason ? reason : "unknown");

                    try
                    {
                        // Read file
                        std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
                        if (!in)
                        {
                            LOGM_ERROR(TAG, "Cannot open routes file: %s (errno: %d)", path.c_str(), errno);
                            return false;
                        }

                        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                        in.close();

                        if (data.empty())
                        {
                            LOGM_WARN(TAG, "Routes file is empty: %s", path.c_str());
                            return false;
                        }

                        LOGM_DEBUG(TAG, "Read %zu bytes from routes file", data.size());

                        // Parse JSON
                        Aws::Crt::JsonObject root(data.c_str());
                        if (!root.WasParseSuccessful())
                        {
                            LOGM_ERROR(TAG, "JSON parse failed: %s", root.GetErrorMessage().c_str());
                            return false;
                        }

                        Aws::Crt::JsonView view = root.View();

                        // Extract routes array using simple pointer (e.g., "/routes")
                        Aws::Crt::JsonView routesView;
                        if (bridgeConfig.routesFileJsonPointer.has_value() && !bridgeConfig.routesFileJsonPointer->empty())
                        {
                            std::string ptr = *bridgeConfig.routesFileJsonPointer;
                            // Remove leading slash if present (e.g., "/routes" -> "routes")
                            if (!ptr.empty() && ptr[0] == '/')
                            {
                                ptr = ptr.substr(1);
                            }

                            if (!view.ValueExists(ptr.c_str()))
                            {
                                LOGM_ERROR(TAG, "JSON pointer key not found: %s", ptr.c_str());
                                return false;
                            }

                            routesView = view.GetJsonObject(ptr.c_str());
                        }
                        else
                        {
                            // No pointer specified, assume root level "routes" key
                            if (!view.ValueExists("routes"))
                            {
                                LOGM_ERROR(TAG, "%s", "No 'routes' key found at root level");
                                return false;
                            }
                            routesView = view.GetJsonObject("routes");
                        }

                        if (!routesView.IsListType())
                        {
                            LOGM_ERROR(TAG, "%s", "Routes element is not an array");
                            return false;
                        }

                        // Parse routes array
                        std::vector<PlainConfig::LocalMqttBridge::Route> newRoutes;
                        auto routesArray = routesView.AsArray();
                        LOGM_INFO(TAG, "Parsing %zu routes from file...", routesArray.size());

                        for (const auto &routeEntry : routesArray)
                        {
                            try
                            {
                                PlainConfig::LocalMqttBridge::Route route;
                                if (routeEntry.ValueExists("direction"))
                                    route.direction = routeEntry.GetString("direction").c_str();
                                if (routeEntry.ValueExists("localTopic"))
                                    route.localTopic = routeEntry.GetString("localTopic").c_str();
                                if (routeEntry.ValueExists("awsTopic"))
                                    route.awsTopic = routeEntry.GetString("awsTopic").c_str();
                                if (routeEntry.ValueExists("localTopicTemplate"))
                                    route.localTopicTemplate = routeEntry.GetString("localTopicTemplate").c_str();
                                if (routeEntry.ValueExists("qos"))
                                    route.qos = routeEntry.GetInteger("qos");
                                if (routeEntry.ValueExists("throttleSeconds"))
                                    route.throttleSeconds = routeEntry.GetInteger("throttleSeconds");
                                newRoutes.push_back(route);
                            }
                            catch (const std::exception& e)
                            {
                                LOGM_WARN(TAG, "Skipping invalid route entry: %s", e.what());
                            }
                        }

                        LOGM_INFO(TAG, "Successfully parsed %zu routes from %s", newRoutes.size(), path.c_str());

                        // Apply the routes
                        applyRoutesAndResubscribe(newRoutes, true);
                        return true;
                    }
                    catch (const std::exception& e)
                    {
                        LOGM_ERROR(TAG, "Exception loading routes: %s", e.what());
                        return false;
                    }
                    catch (...)
                    {
                        LOGM_ERROR(TAG, "%s", "Unknown exception loading routes");
                        return false;
                    }
                }

                void LocalMqttBridgeFeature::applyRoutesAndResubscribe(const std::vector<PlainConfig::LocalMqttBridge::Route>& newRoutes,
                                                                       bool forceResubscribe)
                {
                    LOGM_INFO(TAG, "applyRoutesAndResubscribe called with %zu routes", newRoutes.size());

                    if (!routeMatcher)
                    {
                        LOGM_ERROR(TAG, "%s", "Cannot apply routes: RouteMatcher not initialized");
                        return;
                    }

                    // Replace config routes
                    bridgeConfig.routes = newRoutes;
                    LOGM_INFO(TAG, "%s", "Config routes updated");

                    // Rebuild matcher and topic list
                    std::string tn = getThingName();
                    LOGM_INFO(TAG, "Adding routes to matcher for thing: %s", tn.c_str());

                    try
                    {
                        routeMatcher->addRoutes(bridgeConfig.routes, tn);
                        LOGM_INFO(TAG, "%s", "Routes added to matcher successfully");
                    }
                    catch (const std::exception& e)
                    {
                        LOGM_ERROR(TAG, "Failed to add routes to matcher: %s", e.what());
                        return;
                    }
                    catch (...)
                    {
                        LOGM_ERROR(TAG, "%s", "Unknown exception adding routes to matcher");
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(subscriptionMutex);
                        upRouteLocalTopics.clear();
                        for (const auto &route : bridgeConfig.routes)
                        {
                            if (route.direction == "up" && !route.localTopic.empty())
                            {
                                upRouteLocalTopics.push_back(expandTopic(route.localTopic, tn));
                            }
                        }
                    }
                    // INFO summary once per apply
                    size_t upCount = 0, downCount = 0;
                    for (const auto &route : bridgeConfig.routes) { if (route.direction == "up") upCount++; else if (route.direction == "down") downCount++; }
                    LOGM_INFO(TAG, "Applied %zu routes (%zu up, %zu down) forceResubscribe=%s",
                              bridgeConfig.routes.size(), upCount, downCount,
                              forceResubscribe ? "true" : "false");
                    // Resubscribe/update subscriptions as needed
                    subscribeUpRouteTopicsIfConnected(forceResubscribe);
                }

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws
