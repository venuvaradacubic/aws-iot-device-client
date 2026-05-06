# Local MQTT Bridge Startup Fix

## Issues Fixed

### 1. Routes showing as 0 at startup
**Root cause**: The bridge was loading routes from config during `init()`, but the external routes file (`shadow/local-mqtt-routes-output.json`) didn't exist yet because Sample Shadow hadn't written it.

**Fix**:
- Changed startup sequence to delay connection setup by 30 seconds
- Added clear logging indicating routes will be loaded after delay
- Routes are now loaded from the external file during `setupConnections()` after the delay
- If external file doesn't exist, falls back to input file as designed

### 2. Segmentation fault (SEGV) after ~30 seconds
**Root causes**:
- Processing threads were trying to use connections before they were established
- Insufficient null-pointer guards in critical code paths
- No exception handling around route matcher operations

**Fixes**:
- Start processing threads first (they wait safely for messages)
- Establish connections after 30-second delay in a separate thread
- Added null-pointer guards for `routeMatcher` and `localClient`
- Added try-catch around `routeMatcher->addRoutes()` calls
- Added validation that `routeMatcher` exists before using it
- Improved error logging throughout the startup path

### 3. Infinite restart loop
**Root cause**: SEGV crash caused systemd to restart the service repeatedly

**Fix**: With SEGV fixed, the service will start cleanly and remain running

## Code Changes

### LocalMqttBridgeFeature::start()
- Reordered startup: threads start first, then delayed connection setup
- Changed from marking threads as "pending" to actually starting them immediately
- They safely wait for messages (connections come up async)
- If `setupConnections()` fails after delay, `running` is set to false to stop threads gracefully

### LocalMqttBridgeFeature::setupConnections()
- Added null-pointer check for `routeMatcher`
- Added null-pointer check for `localClient`
- Improved logging when no routes are loaded from external file
- Made it clear when falling back to config routes

### LocalMqttBridgeFeature::reloadRoutesFromExternalIfConfigured()
- Added detailed logging at each step:
  - File path being attempted
  - File existence check
  - Bytes read from file
  - JSON pointer navigation steps
  - Fallback attempts
- Better error messages when file doesn't exist or is empty
- Logs reason for reload (startup, sync, etc.)

### LocalMqttBridgeFeature::applyRoutesAndResubscribe()
- Added null-pointer guard for `routeMatcher`
- Wrapped `routeMatcher->addRoutes()` in try-catch
- Returns early on error instead of crashing

## Expected Log Sequence (Fixed)

```
[INFO] {LocalMqttBridge}: Loaded configuration - enabled: true, routes: 0
[INFO] {LocalMqttBridge}: All bridge components initialized successfully
[INFO] {LocalMqttBridge}: Local MQTT Bridge initialized successfully
[INFO] {LocalMqttBridge}: Starting Local MQTT Bridge (routes from config: 0, external file will be loaded after delay)
[INFO] {LocalMqttBridge}: Local-to-AWS message forwarding thread started
[INFO] {LocalMqttBridge}: AWS-to-local message forwarding thread started
[INFO] {LocalMqttBridge}: Local MQTT Bridge start initiated (connections pending)
[INFO] {LocalMqttBridge}: Waiting 30 seconds for Sample Shadow to prepare routes file...
[INFO] {SampleShadowFeature.cpp}: Output shadow already contains 32 routes; skipping input seed
... (30 seconds later) ...
[INFO] {LocalMqttBridge}: Delay complete, setting up connections and loading routes...
[INFO] {LocalMqttBridge}: Attempting to load routes from: /opt/aws-iot-device-client/shadow/local-mqtt-routes-output.json (reason: startup)
[DEBUG] {LocalMqttBridge}: Read 5432 bytes from routes file
[DEBUG] {LocalMqttBridge}: Using JSON pointer: /current/state/reported/localMqttBridge/routes
[DEBUG] {LocalMqttBridge}: Loaded 32 routes from /opt/aws-iot-device-client/shadow/local-mqtt-routes-output.json (startup)
[INFO] {LocalMqttBridge}: Applied 32 routes (16 up, 16 down) forceResubscribe=true
[INFO] {LocalMqttBridge}: Connections established and routes loaded successfully
```

## Testing Recommendations

1. **Clean start**: Stop service, remove output file, start service
   - Should see: 0 routes initially, 30-second wait, then routes loaded from output or fallback to input

2. **Normal start**: Service with existing output file
   - Should see: Sample Shadow skips input seed, routes load successfully after delay

3. **Check logs**: Look for INFO messages showing:
   - Route count after loading
   - Successful connection establishment
   - No SEGV crashes

4. **Monitor systemd**:
   ```bash
   sudo journalctl -u aws-iot-device-client.service -f
   ```
   Should see clean startup with no restarts

## Files Modified
- `source/local-mqtt-bridge/LocalMqttBridgeFeature.cpp`

## Build Instructions
From workspace root:
```bash
./build-aws-iot-device-client.sh
```

The .deb package will be in the build output directory.
