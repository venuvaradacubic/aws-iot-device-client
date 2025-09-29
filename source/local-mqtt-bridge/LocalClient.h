// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include <chrono>

extern "C" {
#include <mosquitto.h>
}

namespace Aws
{
    namespace Iot
    {
        namespace DeviceClient
        {
            namespace LocalMqttBridge
            {

                /**
                 * \brief Callback function type for received messages
                 * 
                 * \param topic MQTT topic where message was received
                 * \param payload Message payload
                 * \param payloadLen Length of payload in bytes
                 */
                using MessageCallback = std::function<void(const std::string& topic, 
                                                          const void* payload, 
                                                          int payloadLen)>;

                /**
                 * \brief Callback function type for connection status changes
                 * 
                 * \param connected true if connected, false if disconnected
                 * \param reasonCode Connection/disconnection reason code
                 */
                using ConnectionCallback = std::function<void(bool connected, int reasonCode)>;

                /**
                 * \brief MQTT client wrapper for libmosquitto
                 * 
                 * This class provides a simplified interface to the mosquitto MQTT client library
                 * with automatic connection management, reconnection logic, and thread-safe operations.
                 */
                class LocalClient
                {
                    static constexpr char TAG[] = "LocalClient";
                    
                public:
                    /**
                     * \brief Construct a new LocalClient
                     * 
                     * \param clientId MQTT client identifier (empty for auto-generated)
                     */
                    explicit LocalClient(const std::string& clientId = "");
                    
                    /**
                     * \brief Destructor
                     */
                    ~LocalClient();

                    /**
                     * \brief Connect to MQTT broker
                     * 
                     * \param host Broker hostname or IP address
                     * \param port Broker port (default 1883)
                     * \param keepAlive Keep-alive interval in seconds
                     * \param username Username for authentication (empty if not needed)
                     * \param password Password for authentication (empty if not needed)
                     * \return true if connection attempt started successfully
                     */
                    bool connect(const std::string& host, int port = 1883, int keepAlive = 60,
                                const std::string& username = "", const std::string& password = "");

                    /**
                     * \brief Disconnect from broker
                     */
                    void disconnect();

                    /**
                     * \brief Check if client is connected
                     * 
                     * \return true if connected to broker
                     */
                    bool isConnected() const;

                    /**
                     * \brief Subscribe to a topic
                     * 
                     * \param topic MQTT topic pattern to subscribe to
                     * \param qos Quality of service (0, 1, or 2)
                     * \return true if subscription request was sent successfully
                     */
                    bool subscribe(const std::string& topic, int qos = 0);

                    /**
                     * \brief Unsubscribe from a topic
                     * 
                     * \param topic MQTT topic pattern to unsubscribe from
                     * \return true if unsubscription request was sent successfully
                     */
                    bool unsubscribe(const std::string& topic);

                    /**
                     * \brief Publish a message
                     * 
                     * \param topic MQTT topic to publish to
                     * \param payload Message payload
                     * \param payloadLen Length of payload in bytes
                     * \param qos Quality of service (0, 1, or 2)
                     * \param retain Whether to set retain flag
                     * \return true if message was sent successfully
                     */
                    bool publish(const std::string& topic, const void* payload, int payloadLen,
                                int qos = 0, bool retain = false);

                    /**
                     * \brief Publish a string message
                     * 
                     * \param topic MQTT topic to publish to
                     * \param payload String message payload
                     * \param qos Quality of service (0, 1, or 2)
                     * \param retain Whether to set retain flag
                     * \return true if message was sent successfully
                     */
                    bool publish(const std::string& topic, const std::string& payload,
                                int qos = 0, bool retain = false);

                    /**
                     * \brief Set callback for received messages
                     * 
                     * \param callback Function to call when messages are received
                     */
                    void setMessageCallback(MessageCallback callback);

                    /**
                     * \brief Set callback for connection status changes
                     * 
                     * \param callback Function to call on connect/disconnect events
                     */
                    void setConnectionCallback(ConnectionCallback callback);

                    /**
                     * \brief Start the client processing thread
                     * 
                     * This starts the internal thread that handles network I/O and callbacks.
                     * Must be called after setting up callbacks and before connecting.
                     */
                    void start();

                    /**
                     * \brief Stop the client processing thread
                     * 
                     * This stops the internal processing thread and disconnects if connected.
                     */
                    void stop();

                    /**
                     * \brief Get the client ID
                     * 
                     * \return Client identifier string
                     */
                    std::string getClientId() const;

                    /**
                     * \brief Get connection statistics
                     * 
                     * \return String with connection stats (for diagnostics)
                     */
                    std::string getConnectionStats() const;

                private:
                    struct mosquitto* mosq;
                    std::string clientId;
                    std::string host;
                    int port;
                    int keepAlive;
                    std::string username;
                    std::string password;
                    
                    std::atomic<bool> connected;
                    std::atomic<bool> running;
                    std::unique_ptr<std::thread> networkThread;
                    
                    MessageCallback messageCallback;
                    ConnectionCallback connectionCallback;
                    
                    // Connection statistics
                    mutable std::mutex statsMutex;
                    std::chrono::steady_clock::time_point connectTime;
                    std::chrono::steady_clock::time_point lastMessageTime;
                    size_t messagesReceived;
                    size_t messagesSent;
                    int reconnectAttempts;
                    
                    /**
                     * \brief Network processing thread function
                     */
                    void networkThreadFunction();
                    
                    /**
                     * \brief Attempt to reconnect to broker
                     * 
                     * \return true if reconnection was successful
                     */
                    bool attemptReconnect();
                    
                    /**
                     * \brief Initialize mosquitto library (called once)
                     */
                    static void initializeLibrary();
                    
                    /**
                     * \brief Cleanup mosquitto library (called once)
                     */
                    static void cleanupLibrary();

                    // Mosquitto callback functions (static)
                    static void onConnect(struct mosquitto* mosq, void* userdata, int result);
                    static void onDisconnect(struct mosquitto* mosq, void* userdata, int result);
                    static void onMessage(struct mosquitto* mosq, void* userdata, 
                                         const struct mosquitto_message* message);
                    static void onLog(struct mosquitto* mosq, void* userdata, int level, 
                                     const char* str);
                    
                    // Library initialization tracking
                    static std::atomic<bool> libraryInitialized;
                    static std::atomic<int> instanceCount;
                };

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws