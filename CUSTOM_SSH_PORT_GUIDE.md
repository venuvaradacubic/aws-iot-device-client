# Custom SSH Port Configuration for AWS IoT Secure Tunneling

## Overview

This guide explains how to configure AWS IoT Device Client to use a custom SSH port (other than the default port 22) for secure tunneling.

## Problem

By default, the AWS IoT Device Client expects SSH to be running on port 22. If you've changed your SSH daemon to listen on a different port (e.g., port 22010 for security reasons), secure tunneling from the AWS Console will fail because the Device Client still tries to connect to port 22 on your device.

## Solution

We've added a new configurable option `ssh-port` to the `tunneling` section that allows you to specify your custom SSH port.

## Configuration Methods

### Method 1: JSON Configuration File (Recommended)

Edit your Device Client configuration file (typically at `~/.aws-iot-device-client/aws-iot-device-client.conf`):

```json
{
	"endpoint": "your-endpoint.iot.region.amazonaws.com",
	"cert": "/path/to/certificate.pem.crt",
	"key": "/path/to/private.pem.key",
	"root-ca": "/path/to/AmazonRootCA1.pem",
	"thing-name": "your-thing-name",
	"logging": {
		"level": "INFO",
		"type": "FILE",
		"file": "/var/log/aws-iot-device-client/aws-iot-device-client.log"
	},
	"tunneling": {
		"enabled": true,
		"ssh-port": 22010
	}
}
```

**Key configuration:**
- `"ssh-port": 22010` - Replace `22010` with your actual custom SSH port number

### Method 2: Command Line Argument

You can also specify the custom SSH port via command line:

```bash
./aws-iot-device-client \
  --config-file ~/.aws-iot-device-client/aws-iot-device-client.conf \
  --tunneling-ssh-port 22010
```

**Note:** Command line arguments take precedence over JSON configuration.

## Implementation Steps

### 1. Verify Your SSH Configuration

First, confirm which port your SSH daemon is using:

```bash
# Check SSH configuration
sudo grep "^Port" /etc/ssh/sshd_config

# Or verify which port SSH is listening on
sudo netstat -tlnp | grep sshd
# or
sudo ss -tlnp | grep sshd
```

### 2. Update Device Client Configuration

Add or update the `ssh-port` field in the `tunneling` section of your configuration file:

```json
"tunneling": {
    "enabled": true,
    "ssh-port": 22010
}
```

### 3. Rebuild the Device Client

Since you've modified the source code, rebuild the client:

```bash
cd ~/aws-iot-device-client/build
cmake --build . --target aws-iot-device-client
```

### 4. Install the Updated Binary

```bash
sudo systemctl stop aws-iot-device-client
sudo cp build/aws-iot-device-client /usr/local/bin/aws-iot-device-client
# or wherever your binary is installed
```

### 5. Restart the Service

```bash
sudo systemctl restart aws-iot-device-client
sudo systemctl status aws-iot-device-client
```

### 6. Verify the Configuration

Check the logs to confirm the Device Client started successfully:

```bash
tail -f /var/log/aws-iot-device-client/aws-iot-device-client.log
```

## Testing Secure Tunneling

### From AWS Console:

1. Navigate to **AWS IoT Console** → **Manage** → **Tunnels**
2. Click **Create tunnel**
3. Select your thing
4. Choose **SSH** as the service
5. Create the tunnel

### From AWS CLI:

```bash
aws iotsecuretunneling open-tunnel \
  --destination-config thingName=your-thing-name,services=SSH
```

### Test the Connection:

On your laptop/source machine:

1. Download the local proxy if not already installed
2. Start the local proxy with the source access token:
   ```bash
   ./localproxy -r us-east-1 -s 8080 -t <source-access-token>
   ```
3. Connect via SSH through the local proxy:
   ```bash
   ssh -p 8080 username@localhost
   ```

The Device Client on your device will automatically:
- Receive the tunnel notification
- Connect to the secure tunnel endpoint
- Forward traffic to `localhost:22010` (your custom SSH port)

## Port Validation

The configuration validates that the SSH port is:
- Between 1 and 65535
- A valid integer

Invalid port numbers will cause the Device Client to fail startup with an error message in the logs.

## Backward Compatibility

If `ssh-port` is not specified:
- ✅ The Device Client defaults to port 22 (standard SSH port)
- ✅ Existing configurations continue to work without modification
- ✅ No breaking changes to current deployments

## Configuration Priority

If you specify the SSH port in multiple places, the priority order is:

1. **Command line argument** (`--tunneling-ssh-port`) - Highest priority
2. **JSON configuration file** (`"ssh-port": 22010`)
3. **Default value** (port 22) - Used if not specified anywhere

## Troubleshooting

### Tunnel connects but SSH fails

**Problem:** The secure tunnel establishes but SSH connection fails.

**Solutions:**
1. Verify SSH is actually listening on the configured port:
   ```bash
   sudo netstat -tlnp | grep :22010
   ```
2. Check firewall rules allow the custom port
3. Test local SSH connection:
   ```bash
   ssh -p 22010 username@localhost
   ```

### Device Client fails to start

**Problem:** Service fails after configuration change.

**Solutions:**
1. Check the logs for validation errors:
   ```bash
   journalctl -u aws-iot-device-client -n 50
   ```
2. Verify JSON syntax is valid:
   ```bash
   cat ~/.aws-iot-device-client/aws-iot-device-client.conf | jq .
   ```
3. Ensure port number is an integer (not a string)

### Changes not taking effect

**Problem:** Updated configuration but still using port 22.

**Solutions:**
1. Confirm you rebuilt the binary after code changes
2. Verify the service is using the new binary:
   ```bash
   sudo systemctl status aws-iot-device-client
   ```
3. Check that the configuration file path is correct
4. Restart the service:
   ```bash
   sudo systemctl restart aws-iot-device-client
   ```

## Security Considerations

1. **Port Security:** Changing SSH to a non-standard port provides "security through obscurity" but should not replace proper SSH hardening
2. **Firewall Rules:** Ensure your firewall allows the custom SSH port for local connections
3. **Documentation:** Keep track of custom ports in your device configuration documentation

## Files Modified

The following files were modified to add this feature:

- `source/config/Config.h` - Added `sshPort` field and constants
- `source/config/Config.cpp` - Added JSON and CLI loading logic
- `source/tunneling/SecureTunnelingFeature.h` - Added `mSshPort` member variable
- `source/tunneling/SecureTunnelingFeature.cpp` - Modified to use configured port
- `config-template.json` - Added example configuration

## Related AWS Documentation

- [AWS IoT Secure Tunneling](https://docs.aws.amazon.com/iot/latest/developerguide/secure-tunneling.html)
- [Local Proxy Configuration](https://docs.aws.amazon.com/iot/latest/developerguide/local-proxy.html)
- [Device Client Secure Tunneling](source/tunneling/README.md)
