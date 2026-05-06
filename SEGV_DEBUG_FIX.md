# SEGV Crash Debug & Fix

## Problem Analysis

From the logs, the crash (SEGV) occurs exactly at:
```
2025-10-11T14:01:24.226Z [INFO] {LocalMqttBridge}: Attempting to load routes from: /opt/aws-iot-device-client/shadow/local-mqtt-routes-output.json (reason: startup)
```

**No further log messages appear** - the process crashes during JSON parsing/route loading.

## Root Causes

1. **Missing exception handling**: AWS CRT JSON operations can throw exceptions or cause undefined behavior with malformed data
2. **Unsafe JsonView operations**: No guards around `.AsArray()`, `.GetJsonObject()`, etc.
3. **Iterator operations**: File reading with iterators can fail
4. **JSON pointer navigation**: No safety around object traversal

## Changes Made

### 1. Comprehensive Exception Handling

**Added try-catch blocks around**:
- File reading (iterator operations)
- JSON parsing (`Aws::Crt::JsonObject` construction)
- JSON pointer navigation (each step)
- Route array iteration
- Individual route parsing

### 2. Detailed Logging at Each Step

**Before each potentially-crashing operation**:
```cpp
LOGM_INFO(TAG, "Routes file opened successfully, reading content...");
LOGM_INFO(TAG, "Read %zu bytes from routes file", data.size());
LOGM_INFO(TAG, "Parsing JSON...");
LOGM_INFO(TAG, "JSON parsed successfully");
LOGM_INFO(TAG, "Navigating JSON pointer: %s", ptr.c_str());
LOGM_INFO(TAG, "Found routes array via JSON pointer");
LOGM_INFO(TAG, "Starting to parse routes...");
LOGM_INFO(TAG, "Found %zu routes to parse", routesArray.size());
LOGM_INFO(TAG, "Successfully parsed %zu routes", newRoutes.size());
```

This will show **exactly where** the crash occurs.

### 3. Safety Checks

- Check if current element is an object before calling `GetJsonObject()`
- Check if key exists before accessing
- Wrap each route parse in try-catch to continue on individual route errors
- Log warnings instead of crashing on missing fields

### 4. Lambda Capture Fix

Added `this` capture to parseRoutes lambda so it can access `TAG` for logging:
```cpp
auto parseRoutes = [&newRoutes, this](const Aws::Crt::JsonView &rv){
```

## Expected Log Output (Fixed)

With the new logging, you'll see a detailed trace:
```
[INFO] {LocalMqttBridge}: Attempting to load routes from: /opt/.../local-mqtt-routes-output.json (reason: startup)
[INFO] {LocalMqttBridge}: Routes file opened successfully, reading content...
[INFO] {LocalMqttBridge}: Read 5432 bytes from routes file
[INFO] {LocalMqttBridge}: Parsing JSON...
[INFO] {LocalMqttBridge}: JSON parsed successfully
[INFO] {LocalMqttBridge}: Navigating JSON pointer: /current/state/reported/localMqttBridge/routes
[DEBUG] {LocalMqttBridge}: Navigating to: current
[DEBUG] {LocalMqttBridge}: Navigating to: state
[DEBUG] {LocalMqttBridge}: Navigating to: reported
[DEBUG] {LocalMqttBridge}: Navigating to: localMqttBridge
[DEBUG] {LocalMqttBridge}: Navigating to: routes
[INFO] {LocalMqttBridge}: Found routes array via JSON pointer
[INFO] {LocalMqttBridge}: Calling parseRoutes for loaded view...
[INFO] {LocalMqttBridge}: Starting to parse routes...
[INFO] {LocalMqttBridge}: Found 32 routes to parse
[INFO] {LocalMqttBridge}: Successfully parsed 32 routes
[INFO] {LocalMqttBridge}: Applied 32 routes (16 up, 16 down) forceResubscribe=true
```

**If it crashes**, the last log line will show exactly where.

## Debugging Steps

1. **Rebuild with new code**
2. **Deploy and check logs** - look for the last INFO message before crash
3. **Common crash points and fixes**:
   - "Parsing JSON..." → malformed JSON in file
   - "Navigating JSON pointer" → structure mismatch
   - "Found X routes to parse" → issue with individual route data
   - "Successfully parsed X routes" but crash after → issue in `applyRoutesAndResubscribe`

## Additional Issues to Check

### File Path Mismatch
You mentioned routes exist at:
```
/usr/lib/aws-iot-device-client/gate-local-mqtt-routes.json
```

But config points to:
```
/opt/aws-iot-device-client/shadow/local-mqtt-routes-output.json
```

**Action**: Verify which file actually exists and update config if needed.

### Sample Shadow Output
The output file is written by Sample Shadow feature. Check:
```bash
cat /opt/aws-iot-device-client/shadow/local-mqtt-routes-output.json
```

Expected structure:
```json
{
  "current": {
    "state": {
      "reported": {
        "localMqttBridge": {
          "routes": [
            { "direction": "up", "localTopic": "...", "awsTopic": "..." },
            ...
          ]
        }
      }
    }
  }
}
```

## Quick Diagnostics

Run on the device after deploying new build:
```bash
# Check if output file exists and is valid JSON
ls -lh /opt/aws-iot-device-client/shadow/local-mqtt-routes-output.json
cat /opt/aws-iot-device-client/shadow/local-mqtt-routes-output.json | jq .

# Watch logs in real-time
tail -f /var/log/aws-iot-device-client/aws-iot-device-client.log | grep LocalMqttBridge

# Check systemd logs
journalctl -u aws-iot-device-client.service -f
```

## Temporary Workaround

If the crash persists, you can temporarily disable external route loading to isolate:
1. Comment out `routesFile` in config
2. Put routes directly in config JSON under `localMqttBridge.routes`
3. Restart service

This will bypass the JSON file loading entirely.
