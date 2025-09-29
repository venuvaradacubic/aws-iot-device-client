// Copyright Amazon.com, Inc.                 {
                    routeMatcher.reset(new RouteMatcher());
                    // PayloadTagger is a static utility class, no need to instantiateits affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LocalMqttBridgeFeature.h"
#i                                        if (connection)
                            {
                                auto messageHandler = 
                                    [this](const Aws::Crt::Mqtt::MqttConnection&,
                                           const Aws::Crt::String& receivedOnTopic,
                                           const Aws::Crt::ByteBuf& payload) -> void {
                                        this->handleAwsMessage(receivedOnTopic.c_str(), 
                                                              reinterpret_cast<const char*>(payload.buffer), 
                                                              payload.len);
                                    };              auto messageHandler = 
                                    [this](const Aws::Crt::Mqtt::MqttConnection&,
                                           const Aws::Crt::String& receivedOnTopic,
                                           const Aws::Crt::ByteBuf& payload) -> void {
                                        this->handleAwsMessage(receivedOnTopic.c_str(), 
                                                              reinterpret_cast<const char*>(payload.buffer), 
                                                              payload.len);
                                    };              auto messageHandler = 
                                    [this](const Aws::Crt::Mqtt::MqttConnection&,
                                           const Aws::Crt::String& receivedOnTopic,
                                           const Aws::Crt::ByteBuf& payload) -> void {
                                        this->handleAwsMessage(receivedOnTopic.c_str(), 
                                                              reinterpret_cast<const char*>(payload.buffer), 
                                                              payload.len);
                                    };logging/LoggerFactory.h"
#include "ConfigModel.h"
#include <aws/crt/mqtt/MqttConnection.h>
#include <iostream>
#include <chrono>
#include <functional>
#include <mutex>
#include <memory>

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
                    routeMatcher = std::make_unique<RouteMatcher>();
                    // PayloadTagger is a static utility class, no need to instantiate
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

                    LOGM_INFO(TAG, "Starting Local MQTT Bridge with %zu routes", bridgeConfig.routes.size());
                    
                    if (!setupConnections())
                    {
                        LOGM_ERROR(TAG, "%s", "Failed to setup connections");
                        return -1;
                    }

                    // Start processing threads
                    running = true;
                    startTime = std::chrono::steady_clock::now();

                    localToAwsThread.reset(new std::thread(
                        &LocalMqttBridgeFeature::localToAwsThreadFunction, this));
                    awsToLocalThread.reset(new std::thread(
                        &LocalMqttBridgeFeature::awsToLocalThreadFunction, this));

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
                    LOGM_INFO(TAG, "Loaded configuration - enabled: %s, routes: %zu", 
                              bridgeConfig.enabled ? "true" : "false", 
                              bridgeConfig.routes.size());
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
                    // Start local client
                    localClient->start();

                    // Connect to local broker
                    bool connected = localClient->connect(
                        bridgeConfig.local.host,
                        bridgeConfig.local.port,
                        60, // keepAlive seconds
                        bridgeConfig.local.username,
                        bridgeConfig.local.password
                    );

                    if (!connected)
                    {
                        LOGM_ERROR(TAG, "%s", "Failed to connect to local MQTT broker");
                        return false;
                    }

                    // Set up route matcher
                    std::string thingName = getThingName();
                    routeMatcher->addRoutes(bridgeConfig.routes, thingName);

                    // Subscribe to local topics for "up" routes
                    for (const auto& route : bridgeConfig.routes)
                    {
                        if (route.direction == "up" && !route.localTopic.empty())
                        {
                            std::string expandedTopic = expandTopic(route.localTopic, thingName);
                            if (!localClient->subscribe(expandedTopic, route.qos))
                            {
                                LOGM_WARN(TAG, "Failed to subscribe to local topic: %s", expandedTopic.c_str());
                            }
                            else
                            {
                                LOGM_INFO(TAG, "Subscribed to local topic: %s", expandedTopic.c_str());
                            }
                        }
                    }

                    // Subscribe to AWS IoT topics for "down" routes
                    for (const auto& route : bridgeConfig.routes)
                    {
                        if (route.direction == "down" && !route.awsTopic.empty())
                        {
                            std::string expandedTopic = expandTopic(route.awsTopic, thingName);
                            
                            // Subscribe using AWS IoT connection
                            auto connection = resourceManager->getConnection();
                            if (connection)
                            {
                                Aws::Crt::Mqtt::MqttConnection::OnMessageReceivedHandler messageHandler = 
                                    [this](Aws::Crt::Mqtt::MqttConnection&,
                                           const Aws::Crt::String& receivedOnTopic,
                                           const Aws::Crt::ByteBuf& payload,
                                           bool,
                                           Aws::Crt::Mqtt::QOS,
                                           bool) {
                                        this->handleAwsMessage(receivedTopic.c_str(), 
                                                              reinterpret_cast<const char*>(payload.buffer), 
                                                              payload.len);
                                    };
                                
                                auto onSubAck = [this, expandedTopic](const Aws::Crt::Mqtt::MqttConnection&, 
                                                                     uint16_t packetId, 
                                                                     const Aws::Crt::String&,
                                                                     Aws::Crt::Mqtt::QOS qos, 
                                                                     int errorCode) -> void {
                                    if (errorCode == 0)
                                    {
                                        LOGM_INFO(TAG, "Successfully subscribed to AWS IoT topic: %s", expandedTopic.c_str());
                                    }
                                    else
                                    {
                                        LOGM_ERROR(TAG, "Failed to subscribe to AWS IoT topic %s, error: %d", 
                                                  expandedTopic.c_str(), errorCode);
                                    }
                                };
                                
                                uint16_t packetId = connection->Subscribe(expandedTopic.c_str(), 
                                                                         static_cast<Aws::Crt::Mqtt::QOS>(route.qos),
                                                                         messageHandler, 
                                                                         onSubAck);
                                
                                if (packetId == 0)
                                {
                                    LOGM_ERROR(TAG, "Failed to initiate subscription to AWS IoT topic: %s", expandedTopic.c_str());
                                }
                            }
                            else
                            {
                                LOGM_ERROR(TAG, "No AWS IoT connection available for subscription to: %s", expandedTopic.c_str());
                            }
                        }
                    }

                    return true;
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

                            // Find matching route
                            const Route* matchedRoute = routeMatcher->matchUp(message.topic);
                            if (matchedRoute)
                            {
                                // Tag payload to prevent loops
                                std::string taggedPayload = PayloadTagger::tagPayload(
                                    message.payload, "up"
                                );

                                // Expand AWS topic
                                std::string awsTopic = expandTopic(matchedRoute->awsTopic, getThingName());

                                // Publish to AWS IoT Core using resourceManager
                                auto connection = resourceManager->getConnection();
                                if (connection)
                                {
                                    Aws::Crt::ByteBuf payloadBuf = Aws::Crt::ByteBufFromArray(
                                        reinterpret_cast<const uint8_t*>(taggedPayload.data()), 
                                        taggedPayload.size()
                                    );
                                    
                                    auto onPubAck = [this, awsTopic](Aws::Crt::Mqtt::MqttConnection&, 
                                                                        uint16_t packetId, 
                                                                        int errorCode) {
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
                                    
                                    uint16_t packetId = connection->Publish(awsTopic.c_str(),
                                                                           static_cast<Aws::Crt::Mqtt::QOS>(matchedRoute->qos),
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

                void LocalMqttBridgeFeature::handleLocalMessage(const std::string& topic, 
                                                               const void* payload, int payloadLen)
                {
                    if (!running.load())
                    {
                        return;
                    }

                    // Create string from payload with safety checks
                    std::string payloadStr;
                    if (payload != nullptr && payloadLen > 0)
                    {
                        payloadStr = std::string(static_cast<const char*>(payload), payloadLen);
                    }

                    // Check if this is a tagged message (to prevent loops)
                    if (PayloadTagger::isTagged(payloadStr))
                    {
                        LOGM_DEBUG(TAG, "Ignoring tagged message from local broker: %s", topic.c_str());
                        return;
                    }

                    // Queue message for processing
                    if (!localToAwsQueue->push(topic, payloadStr, true))
                    {
                        LOGM_WARN(TAG, "Failed to queue message from local broker: %s", topic.c_str());
                        messagesDropped++;
                    }
                }

                void LocalMqttBridgeFeature::handleAwsMessage(const std::string& topic, 
                                                             const std::string& payload)
                {
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

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws