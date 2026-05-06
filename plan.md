# LocalMqttBridge Feature Implementation Plan

## Overview
Add an optional LocalMqttBridge feature to aws-iot-device-client to bridge selected topics between a local Mosquitto broker and AWS IoT Core. The feature is disabled by default and fully optional at both build and runtime.

## Feature Requirements Summary
- Two-way selective topic forwarding (local→AWS "up", AWS→local "down")
- Explicit route configuration with support for wildcards in AWS topics
- Loop prevention using payload tagging and time-based guards
- Optional lightweight offline queue with heartbeat deduplication
- QoS 0/1 support, QoS 2 downgrade with warning
- Metrics collection with optional periodic publishing
- Time-based throttling to prevent AWS IoT flooding
- CMake build flag: `LOCAL_MQTT_BRIDGE` (default OFF)
- Runtime configuration via JSON config block

## Architecture & Components

### 1. File Structure
```
source/local-mqtt-bridge/
├── README.md
├── LocalMqttBridgeFeature.h
├── LocalMqttBridgeFeature.cpp
├── ConfigModel.h
├── ConfigModel.cpp
├── RouteMatcher.h
├── RouteMatcher.cpp
├── PayloadTagger.h  
├── PayloadTagger.cpp
├── LoopGuard.h
├── LoopGuard.cpp
├── Queue.h
├── Queue.cpp
├── LocalClient.h
├── LocalClient.cpp
└── Metrics.h

test/local-mqtt-bridge/
├── TestRouteMatcher.cpp
├── TestLoopGuard.cpp
├── TestPayloadTagger.cpp
└── TestLocalMqttBridge.cpp
```

### 2. Configuration Schema
```json
{
  "localMqttBridge": {
    "enabled": true,
    "local": {
      "host": "127.0.0.1",
      "port": 1883,
      "useTLS": false,
      "username": "",
      "password": ""
    },
    "routes": [
      {
        "direction": "up",
        "localTopic": "heartbeat/gate",
        "awsTopic": "devices/${thingName}/heartbeat/gate",
        "qos": 0,
        "throttleSeconds": 60
      },
      {
        "direction": "up", 
        "localTopic": "status/gate/mode",
        "awsTopic": "devices/${thingName}/status/gate/mode",
        "qos": 0,
        "throttleSeconds": 30
      },
      {
        "direction": "up",
        "localTopic": "status/gate/faults", 
        "awsTopic": "devices/${thingName}/status/gate/faults",
        "qos": 1,
        "throttleSeconds": 10
      },
      {
        "direction": "up",
        "localTopic": "metrics/gate/+",
        "awsTopic": "devices/${thingName}/metrics/gate/${match1}",
        "qos": 0,
        "throttleSeconds": 300
      },
      {
        "direction": "down",
        "awsTopic": "devices/${thingName}/control/+/#",
        "localTopicTemplate": "control/${match1}/${tail}",
        "qos": 1
      },
      {
        "direction": "down", 
        "awsTopic": "devices/${thingName}/config/+/#",
        "localTopicTemplate": "config/${match1}/${tail}",
        "qos": 1
      }
    ],
    "queue": { 
      "maxInMemory": 200, 
      "dedupeHeartbeat": true 
    },
    "loopGuard": { 
      "ttlSeconds": 5, 
      "maxEntries": 512 
    },
    "metrics": { 
      "publishIntervalSec": 60, 
      "awsTopic": "devices/${thingName}/bridge/metrics", 
      "enabled": true 
    }
  }
}
```

### 3. Core Components

#### 3.1 ConfigModel (ConfigModel.h/cpp)
```cpp
namespace Aws::Iot::DeviceClient::LocalMqttBridge {

struct LocalMqttBridgeConfig : public LoadableFromJsonAndCliAndEnvironment {
    struct LocalBroker {
        std::string host{"127.0.0.1"};
        int port{1883};
        bool useTLS{false};
        std::string username;
        std::string password;
    };
    
    struct Route {
        std::string direction; // "up" or "down"  
        std::string localTopic;
        std::string awsTopic;
        std::string localTopicTemplate;
        int qos{0};
        int throttleSeconds{0}; // For up routes only
    };
    
    struct QueueConfig {
        int maxInMemory{200};
        bool dedupeHeartbeat{true};
    };
    
    struct LoopGuardConfig {
        int ttlSeconds{5};
        int maxEntries{512};
    };
    
    struct MetricsConfig {
        int publishIntervalSec{60};
        std::string awsTopic;
        bool enabled{true};
    };
    
    bool enabled{false};
    LocalBroker local;
    std::vector<Route> routes;
    QueueConfig queue;
    LoopGuardConfig loopGuard;
    MetricsConfig metrics;
    
    // Implement interface methods
    bool LoadFromJson(const Crt::JsonView &json) override;
    bool LoadFromCliArgs(const CliArgs &cliArgs) override;
    bool LoadFromEnvironment() override { return true; }
    bool Validate() const override;
    void SerializeToObject(Crt::JsonObject &object) const;
};

}
```

#### 3.2 RouteMatcher (RouteMatcher.h/cpp)
```cpp
namespace Aws::Iot::DeviceClient::LocalMqttBridge {

struct MatchResult {
    const Route* route;
    std::map<std::string, std::string> variables; // ${match1}, ${tail}, etc.
};

class RouteMatcher {
public:
    void addRoutes(const std::vector<Route>& routes, const std::string& thingName);
    
    // Find up route for exact local topic match
    const Route* matchUp(const std::string& localTopic) const;
    
    // Find down route for AWS topic with wildcard matching  
    std::unique_ptr<MatchResult> matchDown(const std::string& awsTopic) const;
    
private:
    std::vector<Route> upRoutes;
    std::vector<Route> downRoutes; // Compiled patterns
    std::string thingName;
    
    std::string expandVariables(const std::string& template_str, 
                               const std::map<std::string, std::string>& vars) const;
};

}
```

#### 3.3 PayloadTagger (PayloadTagger.h/cpp)
```cpp
namespace Aws::Iot::DeviceClient::LocalMqttBridge {

class PayloadTagger {
public:
    // Tag JSON payload with bridge direction, leave non-JSON unchanged
    static std::string tagPayload(const std::string& payload, const std::string& direction);
    
    // Check if payload already has bridge tag
    static bool isTagged(const std::string& payload);
    
private:
    static bool isValidJson(const std::string& payload);
};

}
```

#### 3.4 LoopGuard (LoopGuard.h/cpp)
```cpp
namespace Aws::Iot::DeviceClient::LocalMqttBridge {

class LoopGuard {
public:
    LoopGuard(int ttlSeconds, int maxEntries);
    
    // Returns true if message should be dropped (is duplicate within TTL)
    bool shouldDrop(const std::string& direction, const std::string& topic, 
                    const std::string& payload);
    
private:
    struct Entry {
        std::string key;
        std::chrono::steady_clock::time_point timestamp;
    };
    
    int ttlSeconds;
    int maxEntries;
    std::unordered_map<std::string, Entry> entries;
    mutable std::mutex mutex;
    
    std::string generateKey(const std::string& direction, const std::string& topic,
                           const std::string& payload) const;
    void cleanup();
};

}
```

#### 3.5 Queue (Queue.h/cpp)
```cpp
namespace Aws::Iot::DeviceClient::LocalMqttBridge {

struct QueuedMessage {
    std::string direction;
    std::string topic;
    std::string payload;
    int qos;
    std::chrono::steady_clock::time_point timestamp;
    bool isHeartbeat{false};
};

class Queue {
public:
    Queue(int maxSize, bool dedupeHeartbeat);
    
    void enqueue(const QueuedMessage& message);
    std::vector<QueuedMessage> dequeueAll();
    size_t size() const;
    
private:
    int maxSize;
    bool dedupeHeartbeat;
    std::deque<QueuedMessage> messages;
    mutable std::mutex mutex;
    
    bool isHeartbeatTopic(const std::string& topic) const;
    void removeOldHeartbeat(const std::string& topic);
    
    // Note: Heartbeat detection is now fully configurable through routes
    // No hardcoded topic patterns in implementation
};

}
```

#### 3.6 LocalClient (LocalClient.h/cpp)
```cpp
namespace Aws::Iot::DeviceClient::LocalMqttBridge {

class LocalClient {
public:
    using MessageCallback = std::function<void(const std::string& topic, 
                                             const std::string& payload, int qos)>;
    
    LocalClient(const LocalMqttBridgeConfig::LocalBroker& config);
    ~LocalClient();
    
    bool connect();
    void disconnect();
    bool isConnected() const;
    
    bool subscribe(const std::string& topic, int qos);
    bool publish(const std::string& topic, const std::string& payload, int qos);
    
    void setMessageCallback(MessageCallback callback);
    
private:
    struct mosquitto* mosq;
    LocalMqttBridgeConfig::LocalBroker config;
    MessageCallback messageCallback;
    std::atomic<bool> connected{false};
    std::thread loopThread;
    mutable std::mutex mutex;
    
    static void onConnect(struct mosquitto* mosq, void* userdata, int result);
    static void onDisconnect(struct mosquitto* mosq, void* userdata, int result);
    static void onMessage(struct mosquitto* mosq, void* userdata, 
                         const struct mosquitto_message* message);
    
    void runLoop();
    void reconnectWithBackoff();
};

}
```

#### 3.7 Metrics (Metrics.h)
```cpp
namespace Aws::Iot::DeviceClient::LocalMqttBridge {

struct BridgeMetrics {
    std::atomic<uint64_t> forwardedUp{0};
    std::atomic<uint64_t> forwardedDown{0};
    std::atomic<uint64_t> droppedLoop{0};
    std::atomic<uint64_t> droppedQos2{0};
    std::atomic<uint64_t> queuedOffline{0};
    std::atomic<uint64_t> publishErrors{0};
    std::atomic<uint64_t> throttledMessages{0};
    
    std::string toJson() const;
    void reset();
};

}
```

#### 3.8 LocalMqttBridgeFeature (LocalMqttBridgeFeature.h/cpp)
```cpp
namespace Aws::Iot::DeviceClient::LocalMqttBridge {

class LocalMqttBridgeFeature : public Feature {
public:
    static constexpr char NAME[] = "Local MQTT Bridge";
    static constexpr char TAG[] = "LocalMqttBridge";
    
    LocalMqttBridgeFeature();
    ~LocalMqttBridgeFeature();
    
    // Feature interface
    std::string getName() override;
    int start() override;
    int stop() override;
    
    // Initialization
    int init(std::shared_ptr<SharedCrtResourceManager> manager,
             std::shared_ptr<ClientBaseNotifier> notifier,
             const PlainConfig& config);
             
private:
    std::shared_ptr<SharedCrtResourceManager> resourceManager;
    std::shared_ptr<ClientBaseNotifier> baseNotifier;
    LocalMqttBridgeConfig bridgeConfig;
    
    std::unique_ptr<RouteMatcher> routeMatcher;
    std::unique_ptr<PayloadTagger> payloadTagger;
    std::unique_ptr<LoopGuard> loopGuard;
    std::unique_ptr<Queue> queue;
    std::unique_ptr<LocalClient> localClient;
    std::unique_ptr<BridgeMetrics> metrics;
    
    std::thread metricsThread;
    std::atomic<bool> running{false};
    
    // Message handling
    void onLocalMessage(const std::string& topic, const std::string& payload, int qos);
    void onAwsMessage(const std::string& topic, const std::string& payload);
    
    // Throttling - fully driven by route configuration
    struct ThrottleState {
        std::chrono::steady_clock::time_point lastSent;
        std::string lastPayload;
    };
    std::unordered_map<std::string, ThrottleState> throttleMap;
    mutable std::mutex throttleMutex;
    
    bool shouldThrottle(const Route* route, const std::string& topic, 
                       const std::string& payload);
    
    // Queue processing
    void drainQueue();
    
    // Metrics publishing
    void publishMetrics();
    void runMetricsLoop();
    
    // Heartbeat detection based on route configuration only
    bool isHeartbeatRoute(const Route* route) const;
};

}
```

## Implementation Steps

### Step 1: CMake Integration
**File: `CMakeLists.txt`**
```cmake
# Add option
option(LOCAL_MQTT_BRIDGE "Enable Local MQTT Bridge feature" OFF)

# Add conditional compilation
if (LOCAL_MQTT_BRIDGE)
    add_definitions(-DLOCAL_MQTT_BRIDGE)
    
    # Find libmosquitto
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)
    
    if(NOT MOSQUITTO_FOUND)
        message(FATAL_ERROR "libmosquitto is required for LOCAL_MQTT_BRIDGE feature. Install libmosquitto-dev or disable with -DLOCAL_MQTT_BRIDGE=OFF")
    endif()
    
    # Add source files
    file(GLOB LOCAL_MQTT_BRIDGE_SRC "source/local-mqtt-bridge/*.cpp")
    list(APPEND DC_SRC ${LOCAL_MQTT_BRIDGE_SRC})
    
    # Add include directories and libraries
    target_include_directories(${DC_PROJECT_NAME} PRIVATE ${MOSQUITTO_INCLUDE_DIRS})
    target_link_libraries(${DC_PROJECT_NAME} ${MOSQUITTO_LIBRARIES})
endif()
```

### Step 2: Config Integration  
**File: `source/config/Config.h`** - Add to PlainConfig struct:
```cpp
#if defined(LOCAL_MQTT_BRIDGE)
struct LocalMqttBridge : public LoadableFromJsonAndCliAndEnvironment {
    // ... config structure as defined above
    
    static constexpr char CLI_ENABLE_LOCAL_MQTT_BRIDGE[] = "--enable-local-mqtt-bridge";
    static constexpr char JSON_KEY_LOCAL_MQTT_BRIDGE[] = "localMqttBridge";
    static constexpr char JSON_KEY_ENABLED[] = "enabled";
    // ... other JSON keys
};
LocalMqttBridge localMqttBridge;
#endif
```

**File: `source/config/Config.cpp`** - Add loading logic:
```cpp
#if defined(LOCAL_MQTT_BRIDGE)
bool PlainConfig::LocalMqttBridge::LoadFromJson(const Crt::JsonView &json) {
    // Implementation
}

bool PlainConfig::LocalMqttBridge::LoadFromCliArgs(const CliArgs &cliArgs) {
    // Implementation  
}

bool PlainConfig::LocalMqttBridge::Validate() const {
    // Implementation
}
#endif

// In PlainConfig::LoadFromCliArgs, add:
#if defined(LOCAL_MQTT_BRIDGE)
    loadFeatureCliArgs = loadFeatureCliArgs && localMqttBridge.LoadFromCliArgs(cliArgs);
#endif

// In PlainConfig::LoadFromJson, add:
#if defined(LOCAL_MQTT_BRIDGE)
    if (json.ValueExists(LocalMqttBridge::JSON_KEY_LOCAL_MQTT_BRIDGE)) {
        localMqttBridge.LoadFromJson(json.GetJsonObject(LocalMqttBridge::JSON_KEY_LOCAL_MQTT_BRIDGE));
    }
#endif
```

### Step 3: Feature Registration
**File: `source/main.cpp`** - Add feature registration:
```cpp
#if defined(LOCAL_MQTT_BRIDGE)
    #include "local-mqtt-bridge/LocalMqttBridgeFeature.h"
    using namespace Aws::Iot::DeviceClient::LocalMqttBridge;
    
    if (config.config.localMqttBridge.enabled) {
        shared_ptr<LocalMqttBridgeFeature> bridge;
        LOG_INFO(TAG, "Local MQTT Bridge is enabled");
        bridge = make_shared<LocalMqttBridgeFeature>();
        bridge->init(resourceManager, listener, config.config);
        features->add(bridge->getName(), bridge);
    } else {
        LOG_INFO(TAG, "Local MQTT Bridge is disabled");
        features->add(LocalMqttBridgeFeature::NAME, nullptr);
    }
#else
    if (config.config.localMqttBridge.enabled) {
        LOGM_ERROR(TAG, "*** %s: Local MQTT Bridge configuration is enabled but feature is not compiled into binary.", DC_FATAL_ERROR);
        deviceClientAbort("Invalid configuration. Local MQTT Bridge configuration is enabled but feature is not compiled into binary.", EXIT_FAILURE);
    }
#endif
```

### Step 4: Core Component Implementation
Implement each component following the class designs above:

1. **ConfigModel.cpp** - JSON parsing, validation, variable expansion
2. **RouteMatcher.cpp** - Topic matching with wildcard support  
3. **PayloadTagger.cpp** - JSON payload tagging for loop prevention
4. **LoopGuard.cpp** - Time-based duplicate detection
5. **Queue.cpp** - Offline message queuing with heartbeat deduplication
6. **LocalClient.cpp** - libmosquitto wrapper with reconnection logic
7. **LocalMqttBridgeFeature.cpp** - Main feature orchestration with throttling

### Step 5: Unit Tests
Create comprehensive tests for each component:

- **TestRouteMatcher.cpp** - Topic matching and variable extraction
- **TestLoopGuard.cpp** - Duplicate detection within TTL
- **TestPayloadTagger.cpp** - JSON tagging and validation
- **TestLocalMqttBridge.cpp** - Integration tests

### Step 6: Documentation
- **source/local-mqtt-bridge/README.md** - Feature overview, configuration, limitations
- **docs/CONFIG.md** - Add LocalMqttBridge configuration section
- **README.md** - Add note about optional Local MQTT Bridge feature

### Step 7: Testing & Validation
1. Build without `LOCAL_MQTT_BRIDGE=ON` → verify no change in behavior
2. Build with `LOCAL_MQTT_BRIDGE=ON` → verify feature compiles
3. Manual testing:
   - Start local mosquitto broker
   - Configure bridge with sample routes  
   - Test local→AWS and AWS→local forwarding
   - Verify loop prevention works
   - Test offline queue and throttling
4. Static analysis and existing tests must pass

## Key Implementation Details

### Throttling Logic
```cpp
bool LocalMqttBridgeFeature::shouldThrottle(const Route* route, 
                                           const std::string& topic, 
                                           const std::string& payload) {
    if (route->throttleSeconds <= 0) return false;
    
    std::lock_guard<std::mutex> lock(throttleMutex);
    auto& state = throttleMap[topic];
    auto now = std::chrono::steady_clock::now();
    
    auto timeSince = std::chrono::duration_cast<std::chrono::seconds>(
        now - state.lastSent).count();
    
    if (timeSince < route->throttleSeconds) {
        // Update payload but don't send yet
        state.lastPayload = payload;
        metrics->throttledMessages++;
        return true;
    }
    
    // Send and update timestamp
    state.lastSent = now;
    state.lastPayload = payload;
    return false;
}
```

### Heartbeat Detection
**No hardcoded topic patterns.** Heartbeat detection is determined by:
1. Route configuration analysis (topics containing "heartbeat" are candidates)
2. User can explicitly mark routes as heartbeat via additional config flag if needed
3. All topic routing is purely config-driven

### Loop Prevention
1. **Payload Tagging**: Add `{"_bridge":"up"}` or `{"_bridge":"down"}` to JSON payloads
2. **LoopGuard**: Track message signatures (direction+topic+payload_hash) with TTL
3. **Drop Tagged Messages**: Skip processing messages that already have bridge tags

### Variable Expansion
- `${thingName}` → configured AWS IoT thing name
- `${match1}`, `${match2}` → wildcard captures from AWS topic patterns  
- `${tail}` → remaining path segments after `#` wildcard

**Example with Gate Topics:**
- AWS topic: `devices/gate001/control/gate/mode` 
- Pattern: `devices/${thingName}/control/+/#`
- Variables: `${match1}="gate"`, `${tail}="mode"`
- Local template: `control/${match1}/${tail}` → `control/gate/mode`

### Topic Configuration Examples
All topics are defined in JSON configuration only. No hardcoded patterns in C++ code.

**Up Routes (Local → AWS):**
- `heartbeat/gate` → `devices/gate001/heartbeat/gate` (throttled 60s)
- `status/gate/mode` → `devices/gate001/status/gate/mode` (throttled 30s)  
- `metrics/gate/+` → `devices/gate001/metrics/gate/${match1}` (throttled 5min)

**Down Routes (AWS → Local):**
- `devices/gate001/control/+/#` → `control/${match1}/${tail}`
- `devices/gate001/config/+/#` → `config/${match1}/${tail}`

### Error Handling
- **Connection Loss**: Queue messages until reconnection
- **QoS 2 Messages**: Log warning, downgrade to QoS 1 or drop
- **Invalid JSON**: Forward as-is without tagging
- **Topic Mismatch**: Log and ignore
- **Throttling**: Store latest payload, send after delay

## Acceptance Criteria
✅ Feature disabled by default at build and runtime
✅ No behavior change when compiled without `LOCAL_MQTT_BRIDGE=ON`
✅ Successful bidirectional message forwarding
✅ Loop prevention prevents infinite message cycles  
✅ Throttling prevents AWS IoT flooding from frequent local updates
✅ Offline queue preserves messages during AWS disconnection
✅ QoS handling with appropriate warnings/downgrades
✅ Metrics tracking and optional publishing
✅ Unit tests covering core components
✅ Documentation updated
✅ Static analysis passes

This plan provides a complete implementation roadmap for the LocalMqttBridge feature while maintaining clean separation of concerns and following existing codebase patterns.