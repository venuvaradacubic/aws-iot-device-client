# Quick Start: Using Port 443 for AWS IoT Device Client

## For Users in Restrictive Networks

If your production network only allows port 443 (HTTPS) for outbound connections, follow these steps:

## Step 1: Update Your Configuration File

Edit your configuration file (e.g., `~/.aws-iot-device-client/aws-iot-device-client.conf`):

```json
{
  "endpoint": "xxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com",
  "cert": "/etc/iot/certificate.pem.crt",
  "key": "/etc/iot/private.pem.key",
  "root-ca": "/etc/iot/AmazonRootCA1.pem",
  "thing-name": "MyDevice",
  "mqtt-port": 443,
  "logging": {
    "level": "INFO",
    "type": "FILE",
    "file": "/var/log/aws-iot-device-client/aws-iot-device-client.log"
  },
  "jobs": {
    "enabled": true
  },
  "tunneling": {
    "enabled": true
  }
}
```

**Key change**: Add `"mqtt-port": 443` to your configuration.

## Step 2: Run the Device Client

```bash
./aws-iot-device-client
```

Or with command line override:

```bash
./aws-iot-device-client --mqtt-port 443
```

## Step 3: Verify Connection

Check your logs for these messages:

```
[INFO] Using MQTT port override: 443
[INFO] Port 443 detected, using ALPN protocol: x-amzn-mqtt-ca
[INFO] MQTT connection established with return code: 0
```

## That's It!

Your device is now connecting to AWS IoT Core using port 443 instead of 8883.

## Before vs After

### Before (Default - Port 8883)
```json
{
  "endpoint": "xxxxxxxxxxxxx.iot.us-east-1.amazonaws.com",
  "cert": "/path/to/cert.pem.crt",
  "key": "/path/to/key.pem.key",
  "thing-name": "MyDevice"
}
```

### After (Port 443)
```json
{
  "endpoint": "xxxxxxxxxxxxx.iot.us-east-1.amazonaws.com",
  "cert": "/path/to/cert.pem.crt",
  "key": "/path/to/key.pem.key",
  "thing-name": "MyDevice",
  "mqtt-port": 443
}
```

## Troubleshooting

**Problem**: Connection times out  
**Solution**: Verify your firewall allows outbound HTTPS on port 443

**Problem**: TLS error  
**Solution**: Ensure your certificate and endpoint are correct

**Problem**: Still getting blocked  
**Solution**: Your network may require an HTTP proxy. See [HTTP_PROXY.md](docs/HTTP_PROXY.md)

## Need More Details?

See the complete documentation: [PORT_443_CONFIGURATION.md](docs/PORT_443_CONFIGURATION.md)
