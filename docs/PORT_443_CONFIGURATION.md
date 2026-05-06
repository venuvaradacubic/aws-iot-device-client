# MQTT Port 443 Configuration for AWS IoT Core

**Notice:** Running the AWS IoT Device Client will incur usage of AWS IoT services, and is likely to incur charges on your AWS account. Please refer the pricing pages for [AWS IoT Core](https://aws.amazon.com/iot-core/pricing/), [AWS IoT Device Management](https://aws.amazon.com/iot-device-management/pricing/), and [AWS IoT Device Defender](https://aws.amazon.com/iot-device-defender/pricing/) for more details.

[*Back To The Main Readme*](../README.md)

## Overview

By default, AWS IoT Core uses port **8883** for MQTT connections. However, in restrictive network environments where only standard HTTPS port **443** is allowed for outbound connections, AWS IoT Core supports MQTT over port 443 using **ALPN (Application Layer Protocol Negotiation)**.

This feature allows the AWS IoT Device Client to connect to AWS IoT Core using port 443, enabling deployment in networks with strict firewall rules.

## How It Works

When you configure the Device Client to use port 443:

1. The MQTT connection uses TLS with port 443 instead of the default 8883
2. ALPN protocol negotiation is used to establish the MQTT protocol over HTTPS
3. The default ALPN protocol name for AWS IoT Core is `x-amzn-mqtt-ca`
4. The connection uses the same endpoint as port 8883 (e.g., `xxxxxxxxxxxxx.iot.us-east-1.amazonaws.com`)

## Configuration

### Via JSON Configuration File

Add the following fields to your configuration file (e.g., `~/.aws-iot-device-client/aws-iot-device-client.conf`):

```json
{
  "endpoint": "xxxxxxxxxxxxx.iot.us-east-1.amazonaws.com",
  "cert": "/path/to/certificate.pem.crt",
  "key": "/path/to/private.pem.key",
  "root-ca": "/path/to/AmazonRootCA1.pem",
  "thing-name": "my-iot-device",
  "mqtt-port": 443,
  "mqtt-alpn": "x-amzn-mqtt-ca",
  ...
}
```

### Configuration Options

**`mqtt-port`** (optional): The MQTT port to use for connections
- Type: Integer
- Default: 8883 (if not specified)
- Valid range: 1-65535
- **Use 443 for restrictive network environments**

**`mqtt-alpn`** (optional): The ALPN protocol name for TLS negotiation
- Type: String
- Default: `x-amzn-mqtt-ca` (automatically used when port is 443)
- Only relevant when using port 443
- AWS IoT Core ALPN protocol: `x-amzn-mqtt-ca`

### Via Command Line Arguments

You can override the configuration file settings using command line arguments:

```bash
./aws-iot-device-client \
  --endpoint xxxxxxxxxxxxx.iot.us-east-1.amazonaws.com \
  --cert /path/to/certificate.pem.crt \
  --key /path/to/private.pem.key \
  --root-ca /path/to/AmazonRootCA1.pem \
  --thing-name my-iot-device \
  --mqtt-port 443 \
  --mqtt-alpn x-amzn-mqtt-ca
```

## Use Cases

### Restrictive Corporate Networks

Many corporate networks only allow outbound connections on standard ports:
- Port 80 (HTTP)
- Port 443 (HTTPS)

By configuring MQTT to use port 443, IoT devices behind such firewalls can still connect to AWS IoT Core without requiring special firewall exceptions.

### Example Configuration for Port 443

**Minimal configuration for port 443:**

```json
{
  "endpoint": "a3xyzzyx0000ab-ats.iot.us-east-1.amazonaws.com",
  "cert": "/etc/iot-device/certificate.pem.crt",
  "key": "/etc/iot-device/private.pem.key",
  "root-ca": "/etc/iot-device/AmazonRootCA1.pem",
  "thing-name": "ProductionDevice001",
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

**Note:** The `mqtt-alpn` field can be omitted when using port 443, as it will automatically default to `x-amzn-mqtt-ca`.

## Comparison: Port 8883 vs Port 443

| Feature | Port 8883 (Default) | Port 443 (ALPN) |
|---------|-------------------|-----------------|
| Protocol | MQTT over TLS | MQTT over TLS with ALPN |
| Firewall Compatibility | May be blocked | Works with HTTPS rules |
| Configuration | No extra config needed | Requires `mqtt-port: 443` |
| Performance | Standard | Slightly higher latency |
| Use Case | Standard deployments | Restrictive networks |

## Troubleshooting

### Connection Issues

If you experience connection issues when using port 443:

1. **Verify endpoint is correct**: The endpoint should be the same as for port 8883
2. **Check certificate validity**: Ensure your device certificate is valid and not expired
3. **Verify network allows port 443**: Confirm outbound HTTPS connections are allowed
4. **Check logs**: Enable DEBUG logging to see detailed connection information

```json
{
  "logging": {
    "level": "DEBUG",
    "type": "STDOUT"
  }
}
```

### Expected Log Messages

When successfully connecting with port 443, you should see:

```
[INFO] Using MQTT port override: 443
[INFO] Port 443 detected, using ALPN protocol: x-amzn-mqtt-ca
[INFO] MQTT connection established with return code: 0
```

### Common Errors

**Error: Connection timeout**
- Verify port 443 is not blocked by your firewall
- Check if your network requires an HTTP proxy (see [HTTP_PROXY.md](HTTP_PROXY.md))

**Error: TLS negotiation failed**
- Ensure the ALPN protocol name is correct (`x-amzn-mqtt-ca`)
- Verify your AWS IoT endpoint URL is correct

## Testing Port 443 Connection

You can test if port 443 MQTT is working using openssl:

```bash
openssl s_client -connect xxxxxxxxxxxxx.iot.us-east-1.amazonaws.com:443 \
  -CAfile /path/to/AmazonRootCA1.pem \
  -cert /path/to/certificate.pem.crt \
  -key /path/to/private.pem.key \
  -alpn x-amzn-mqtt-ca
```

A successful connection should show:
```
CONNECTED(00000003)
...
ALPN protocol: x-amzn-mqtt-ca
...
Verify return code: 0 (ok)
```

## Combining with HTTP Proxy

Port 443 configuration can be combined with HTTP proxy settings for environments that require both:

```json
{
  "endpoint": "xxxxxxxxxxxxx.iot.us-east-1.amazonaws.com",
  "mqtt-port": 443,
  "mqtt-alpn": "x-amzn-mqtt-ca",
  "http-proxy-config": "~/.aws-iot-device-client/http-proxy.conf"
}
```

See [HTTP_PROXY.md](HTTP_PROXY.md) for more details on HTTP proxy configuration.

## References

- [AWS IoT Core - Protocols](https://docs.aws.amazon.com/iot/latest/developerguide/protocols.html)
- [AWS IoT Core - Connection using ALPN](https://docs.aws.amazon.com/iot/latest/developerguide/protocols.html#alpn)
- [Device Client Configuration](CONFIG.md)

[*Back To The Top*](#mqtt-port-443-configuration-for-aws-iot-core)
