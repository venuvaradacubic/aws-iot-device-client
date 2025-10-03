// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LocalClient.h"
#include "../logging/LoggerFactory.h"
#include <sstream>
#include <iomanip>
#include <random>

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
                constexpr char LocalClient::TAG[];

                // Static members
                std::atomic<bool> LocalClient::libraryInitialized{false};
                std::atomic<int> LocalClient::instanceCount{0};

                LocalClient::LocalClient(const std::string& clientId)
                    : mosq(nullptr), clientId(clientId), port(1883), keepAlive(60),
                      connected(false), running(false),
                      messagesReceived(0), messagesSent(0), reconnectAttempts(0)
                {
                    initializeLibrary();
                    instanceCount++;

                    // Generate client ID if not provided
                    if (this->clientId.empty())
                    {
                        std::random_device rd;
                        std::mt19937 gen(rd());
                        std::uniform_int_distribution<> dis(1000, 9999);
                        this->clientId = "aws-iot-device-client-" + std::to_string(dis(gen));
                    }

                    // Create mosquitto instance
                    mosq = mosquitto_new(this->clientId.c_str(), true, this);
                    if (!mosq)
                    {
                        LOGM_ERROR(TAG, "%s", "Failed to create mosquitto instance");
                        throw std::runtime_error("Failed to create mosquitto instance");
                    }

                    // Set callbacks
                    mosquitto_connect_callback_set(mosq, onConnect);
                    mosquitto_disconnect_callback_set(mosq, onDisconnect);
                    mosquitto_message_callback_set(mosq, onMessage);
                    mosquitto_log_callback_set(mosq, onLog);

                    LOGM_INFO(TAG, "LocalClient created with ID: %s", this->clientId.c_str());
                }

                LocalClient::~LocalClient()
                {
                    stop();

                    if (mosq)
                    {
                        mosquitto_destroy(mosq);
                        mosq = nullptr;
                    }

                    instanceCount--;
                    if (instanceCount == 0)
                    {
                        cleanupLibrary();
                    }

                    LOGM_INFO(TAG, "LocalClient destroyed: %s", clientId.c_str());
                }

                bool LocalClient::connect(const std::string& host, int port, int keepAlive,
                                         const std::string& username, const std::string& password)
                {
                    if (!mosq)
                    {
                        LOGM_ERROR(TAG, "%s", "Mosquitto instance not initialized");
                        return false;
                    }

                    this->host = host;
                    this->port = port;
                    this->keepAlive = keepAlive;
                    this->username = username;
                    this->password = password;

                    // Set authentication if provided
                    if (!username.empty())
                    {
                        int result = mosquitto_username_pw_set(mosq, username.c_str(),
                                                              password.empty() ? nullptr : password.c_str());
                        if (result != MOSQ_ERR_SUCCESS)
                        {
                            LOGM_ERROR(TAG, "Failed to set authentication: %s", mosquitto_strerror(result));
                            return false;
                        }
                    }

                    // Attempt connection
                    int result = mosquitto_connect(mosq, host.c_str(), port, keepAlive);
                    if (result != MOSQ_ERR_SUCCESS)
                    {
                        LOGM_ERROR(TAG, "Failed to connect to %s:%d - %s",
                                  host.c_str(), port, mosquitto_strerror(result));
                        return false;
                    }

                    LOGM_INFO(TAG, "Connecting to %s:%d with client ID %s",
                             host.c_str(), port, clientId.c_str());
                    return true;
                }

                void LocalClient::disconnect()
                {
                    if (mosq && connected.load())
                    {
                        mosquitto_disconnect(mosq);
                        LOGM_INFO(TAG, "%s", "Disconnected from broker");
                    }
                }

                bool LocalClient::isConnected() const
                {
                    return connected.load();
                }

                bool LocalClient::subscribe(const std::string& topic, int qos)
                {
                    if (!mosq || !connected.load())
                    {
                        LOGM_WARN(TAG, "%s", "Cannot subscribe - not connected");
                        return false;
                    }

                    int result = mosquitto_subscribe(mosq, nullptr, topic.c_str(), qos);
                    if (result != MOSQ_ERR_SUCCESS)
                    {
                        LOGM_ERROR(TAG, "Failed to subscribe to %s: %s",
                                  topic.c_str(), mosquitto_strerror(result));
                        return false;
                    }

                    LOGM_DEBUG(TAG, "Subscribed to topic: %s (QoS %d)", topic.c_str(), qos);
                    return true;
                }

                bool LocalClient::unsubscribe(const std::string& topic)
                {
                    if (!mosq || !connected.load())
                    {
                        LOGM_WARN(TAG, "%s", "Cannot unsubscribe - not connected");
                        return false;
                    }

                    int result = mosquitto_unsubscribe(mosq, nullptr, topic.c_str());
                    if (result != MOSQ_ERR_SUCCESS)
                    {
                        LOGM_ERROR(TAG, "Failed to unsubscribe from %s: %s",
                                  topic.c_str(), mosquitto_strerror(result));
                        return false;
                    }

                    LOGM_DEBUG(TAG, "Unsubscribed from topic: %s", topic.c_str());
                    return true;
                }

                bool LocalClient::publish(const std::string& topic, const void* payload, int payloadLen,
                                         int qos, bool retain)
                {
                    if (!mosq || !connected.load())
                    {
                        LOGM_WARN(TAG, "%s", "Cannot publish - not connected");
                        return false;
                    }

                    int result = mosquitto_publish(mosq, nullptr, topic.c_str(), payloadLen, payload, qos, retain);
                    if (result != MOSQ_ERR_SUCCESS)
                    {
                        LOGM_ERROR(TAG, "Failed to publish to %s: %s",
                                  topic.c_str(), mosquitto_strerror(result));
                        return false;
                    }

                    {
                        std::lock_guard<std::mutex> lock(statsMutex);
                        messagesSent++;
                        lastMessageTime = std::chrono::steady_clock::now();
                    }

                    LOGM_DEBUG(TAG, "Published message to %s (payload=%d bytes, QoS=%d, retain=%s)",
                              topic.c_str(), payloadLen, qos, retain ? "true" : "false");
                    return true;
                }

                bool LocalClient::publish(const std::string& topic, const std::string& payload,
                                         int qos, bool retain)
                {
                    return publish(topic, payload.c_str(), static_cast<int>(payload.length()), qos, retain);
                }

                void LocalClient::setMessageCallback(MessageCallback callback)
                {
                    messageCallback = std::move(callback);
                }

                void LocalClient::setConnectionCallback(ConnectionCallback callback)
                {
                    connectionCallback = std::move(callback);
                }

                void LocalClient::start()
                {
                    if (running.load())
                    {
                        LOGM_WARN(TAG, "%s", "LocalClient already running");
                        return;
                    }

                    running = true;
                    networkThread.reset(new std::thread(&LocalClient::networkThreadFunction, this));
                    LOGM_INFO(TAG, "%s", "LocalClient network thread started");
                }

                void LocalClient::stop()
                {
                    if (!running.load())
                    {
                        return;
                    }

                    running = false;
                    disconnect();

                    if (networkThread && networkThread->joinable())
                    {
                        networkThread->join();
                        networkThread.reset();
                    }

                    LOGM_INFO(TAG, "%s", "LocalClient stopped");
                }

                std::string LocalClient::getClientId() const
                {
                    return clientId;
                }

                std::string LocalClient::getConnectionStats() const
                {
                    std::lock_guard<std::mutex> lock(statsMutex);

                    std::stringstream ss;
                    ss << "LocalClient[" << clientId << "] ";
                    ss << "Connected: " << (connected.load() ? "true" : "false") << ", ";
                    ss << "Host: " << host << ":" << port << ", ";
                    ss << "Messages RX: " << messagesReceived << ", ";
                    ss << "Messages TX: " << messagesSent << ", ";
                    ss << "Reconnects: " << reconnectAttempts;

                    if (connected.load())
                    {
                        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - connectTime);
                        ss << ", Uptime: " << uptime.count() << "s";
                    }

                    return ss.str();
                }

                void LocalClient::networkThreadFunction()
                {
                    LOGM_INFO(TAG, "Network thread started for client %s", clientId.c_str());

                    // Initialize timing so first attempt can start immediately
                    nextAttemptTime = std::chrono::steady_clock::now();

                    while (running.load())
                    {
                        auto now = std::chrono::steady_clock::now();

                        switch (state.load())
                        {
                            case ConnState::Connected:
                            {
                                int result = mosquitto_loop(mosq, 100, 1);
                                if (result != MOSQ_ERR_SUCCESS)
                                {
                                    LOGM_WARN(TAG, "Network loop returned %s; marking disconnected", mosquitto_strerror(result));
                                    setState(ConnState::Disconnected);
                                    scheduleBackoff();
                                }
                                break;
                            }
                            case ConnState::Connecting:
                            {
                                int result = mosquitto_loop(mosq, 100, 1);
                                if (result != MOSQ_ERR_SUCCESS)
                                {
                                    LOGM_WARN(TAG, "Loop while connecting returned %s; will backoff", mosquitto_strerror(result));
                                    setState(ConnState::Disconnected);
                                    scheduleBackoff();
                                    break;
                                }
                                // Timeout if no CONNACK within CONNECT_TIMEOUT_SEC
                                if (now - connectingStartTime > std::chrono::seconds(CONNECT_TIMEOUT_SEC))
                                {
                                    LOGM_WARN(TAG, "Connect timeout (%ds) reached without CONNACK; forcing retry", CONNECT_TIMEOUT_SEC);
                                    setState(ConnState::Disconnected);
                                    scheduleBackoff();
                                }
                                break;
                            }
                            case ConnState::Disconnected:
                            {
                                if (now >= nextAttemptTime && !host.empty())
                                {
                                    attemptConnect();
                                }
                                else
                                {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                }
                                break;
                            }
                        }
                    }

                    LOGM_INFO(TAG, "Network thread stopped for client %s", clientId.c_str());
                }

                void LocalClient::setState(ConnState newState)
                {
                    state.store(newState);
                    connected.store(newState == ConnState::Connected);
                }

                void LocalClient::scheduleBackoff(bool immediateOnFailure)
                {
                    if (immediateOnFailure)
                    {
                        currentBackoffSeconds = INITIAL_BACKOFF_SEC;
                    }
                    else if (currentBackoffSeconds == 0)
                    {
                        currentBackoffSeconds = INITIAL_BACKOFF_SEC;
                    }
                    else
                    {
                        // Exponential growth
                        currentBackoffSeconds = std::min(currentBackoffSeconds * 2, MAX_BACKOFF_SEC);
                    }

                    // Add small jitter (0-250ms) to avoid lockstep with other clients
                    static thread_local std::mt19937 rng{std::random_device{}()};
                    std::uniform_int_distribution<int> jitterDist(0, 250);
                    auto jitterMs = std::chrono::milliseconds(jitterDist(rng));

                    nextAttemptTime = std::chrono::steady_clock::now() + std::chrono::seconds(currentBackoffSeconds) + jitterMs;
                    LOGM_INFO(TAG, "Next reconnect attempt in %d s (+%lld ms jitter)", currentBackoffSeconds, (long long)jitterMs.count());
                }

                bool LocalClient::attemptConnect()
                {
                    if (!mosq || !running.load())
                    {
                        return false;
                    }
                    if (state.load() == ConnState::Connecting || state.load() == ConnState::Connected)
                    {
                        return true; // Already in progress or done
                    }

                    setState(ConnState::Connecting);
                    connectingStartTime = std::chrono::steady_clock::now();

                    LOGM_INFO(TAG, "Attempting connection to %s:%d (backoffAttempt=%d)", host.c_str(), port, backoffAttempt);
                    int result = mosquitto_connect(mosq, host.c_str(), port, keepAlive);
                    if (result == MOSQ_ERR_SUCCESS)
                    {
                        {
                            std::lock_guard<std::mutex> lock(statsMutex);
                            reconnectAttempts++; // counts initiated attempts beyond the first
                        }
                        backoffAttempt++;
                        // Do not mark connected yet; wait for onConnect callback
                        return true;
                    }
                    else
                    {
                        LOGM_WARN(TAG, "Connect initiation failed: %s", mosquitto_strerror(result));
                        {
                            std::lock_guard<std::mutex> lock(statsMutex);
                            reconnectFailures++;
                        }
                        setState(ConnState::Disconnected);
                        scheduleBackoff();
                        return false;
                    }
                }

                void LocalClient::initializeLibrary()
                {
                    if (!libraryInitialized.load())
                    {
                        int result = mosquitto_lib_init();
                        if (result != MOSQ_ERR_SUCCESS)
                        {
                            LOGM_ERROR(TAG, "Failed to initialize mosquitto library: %s",
                                      mosquitto_strerror(result));
                            throw std::runtime_error("Failed to initialize mosquitto library");
                        }

                        libraryInitialized = true;
                        LOGM_INFO(TAG, "%s", "Mosquitto library initialized");
                    }
                }

                void LocalClient::cleanupLibrary()
                {
                    if (libraryInitialized.load())
                    {
                        mosquitto_lib_cleanup();
                        libraryInitialized = false;
                        LOGM_INFO(TAG, "%s", "Mosquitto library cleaned up");
                    }
                }

                // Static callback functions
                void LocalClient::onConnect(struct mosquitto* mosq, void* userdata, int result)
                {
                    LocalClient* client = static_cast<LocalClient*>(userdata);
                    if (!client) return;

                    if (result == 0)
                    {
                        client->setState(ConnState::Connected);
                        client->currentBackoffSeconds = INITIAL_BACKOFF_SEC; // reset backoff window
                        client->backoffAttempt = 0;
                        {
                            std::lock_guard<std::mutex> lock(client->statsMutex);
                            client->connectTime = std::chrono::steady_clock::now();
                            client->connectSuccesses++;
                        }

                        LOGM_INFO(TAG, "Connected to broker: %s", client->clientId.c_str());

                        if (client->connectionCallback)
                        {
                            client->connectionCallback(true, result);
                        }
                    }
                    else
                    {
                        LOGM_ERROR(TAG, "Connection failed for %s: %s",
                                  client->clientId.c_str(), mosquitto_strerror(result));

                        if (client->connectionCallback)
                        {
                            client->connectionCallback(false, result);
                        }
                    }
                }

                void LocalClient::onDisconnect(struct mosquitto* mosq, void* userdata, int result)
                {
                    LocalClient* client = static_cast<LocalClient*>(userdata);
                    if (!client) return;

                    client->setState(ConnState::Disconnected);
                    client->scheduleBackoff(true);

                    LOGM_INFO(TAG, "Disconnected from broker: %s (reason: %s)",
                             client->clientId.c_str(), mosquitto_strerror(result));

                    if (client->connectionCallback)
                    {
                        client->connectionCallback(false, result);
                    }
                }

                void LocalClient::onMessage(struct mosquitto* mosq, void* userdata,
                                           const struct mosquitto_message* message)
                {
                    LocalClient* client = static_cast<LocalClient*>(userdata);
                    if (!client || !message) return;

                    {
                        std::lock_guard<std::mutex> lock(client->statsMutex);
                        client->messagesReceived++;
                        client->lastMessageTime = std::chrono::steady_clock::now();
                    }

                    LOGM_DEBUG(TAG, "Message received on %s (payload=%d bytes)",
                              message->topic, message->payloadlen);

                    if (client->messageCallback)
                    {
                        client->messageCallback(std::string(message->topic),
                                              message->payload, message->payloadlen);
                    }
                }

                void LocalClient::onLog(struct mosquitto* mosq, void* userdata, int level, const char* str)
                {
                    LocalClient* client = static_cast<LocalClient*>(userdata);
                    if (!client || !str) return;

                    // Map mosquitto log levels to our log levels
                    switch (level)
                    {
                        case MOSQ_LOG_ERR:
                            LOGM_ERROR(TAG, "Mosquitto[%s]: %s", client->clientId.c_str(), str);
                            break;
                        case MOSQ_LOG_WARNING:
                            LOGM_WARN(TAG, "Mosquitto[%s]: %s", client->clientId.c_str(), str);
                            break;
                        case MOSQ_LOG_NOTICE:
                        case MOSQ_LOG_INFO:
                            LOGM_INFO(TAG, "Mosquitto[%s]: %s", client->clientId.c_str(), str);
                            break;
                        case MOSQ_LOG_DEBUG:
                        default:
                            LOGM_DEBUG(TAG, "Mosquitto[%s]: %s", client->clientId.c_str(), str);
                            break;
                    }
                }

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws
