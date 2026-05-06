#!/bin/bash

if [ -z "$BASH_VERSION" ]; then
    exec bash "$0" "$@"
fi

set -euo pipefail

AIDC_VER=${AIDC_VER:-local-mqtt-bridge}
ARCH=${ARCH:-arm64}
CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release}


ROOT_DIR=${PWD}
BUILD_DIR=${ROOT_DIR}/build-aidc
SRC_DIR=${BUILD_DIR}/aws-iot-device-client
PKG_DIR=${BUILD_DIR}/debian-package
SERVICE_NAME=aws-iot-device-client


rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}" "${PKG_DIR}"

sudo apt-get update

sudo apt-get install -y \
  build-essential cmake git pkg-config \
  libssl-dev libcurl4-openssl-dev zlib1g-dev \
  libc6-dev \
  libmosquitto-dev \
  ca-certificates jq


MEM_TOTAL_MB=$(awk '/MemTotal/ {printf "%.0f", $2/1024}' /proc/meminfo)
SWAP_TOTAL_MB=$(awk '/SwapTotal/ {printf "%.0f", $2/1024}' /proc/meminfo)
if [ "${MEM_TOTAL_MB}" -lt 2560 ] && [ "${SWAP_TOTAL_MB}" -eq 0 ]; then
  SWAP_FILE="/var/tmp/aws-iot-build-swap"
  echo "Detected low RAM (${MEM_TOTAL_MB}MB) and no swap. Enabling 2GB swap at ${SWAP_FILE}..."
  if ! sudo test -f "${SWAP_FILE}"; then
    sudo dd if=/dev/zero of="${SWAP_FILE}" bs=1M count=2048
    sudo chmod 600 "${SWAP_FILE}"
    sudo mkswap "${SWAP_FILE}"
  fi
  sudo swapon "${SWAP_FILE}" || true
  free -m
else
  echo "Memory check: RAM=${MEM_TOTAL_MB}MB, Swap=${SWAP_TOTAL_MB}MB (no swap creation needed)."
fi


cd "${BUILD_DIR}"
git clone https://github.com/venuvaradacubic/aws-iot-device-client.git "${SRC_DIR}"
cd "${SRC_DIR}"
git checkout "${AIDC_VER}"


if grep -Eq "^[[:space:]]*add_subdirectory\([[:space:]]*test[[:space:]]*\)" CMakeLists.txt; then
  echo "Disabling upstream test build (removing add_subdirectory(test))"
  sudo sed -Ei 's/^[[:space:]]*add_subdirectory\([[:space:]]*test[[:space:]]*\)/# add_subdirectory(test) disabled by packaging build/g' CMakeLists.txt
fi


if [ -d test ]; then
  echo "Removing test/ directory to avoid GTest requirement"
  rm -rf test
fi


mkdir -p build
cd build

cmake -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
      -DBUILD_TESTING=OFF \
      -DBUILD_TESTS=OFF \
      -DBUILD_TEST_DEPS=OFF \
      -DLOCAL_MQTT_BRIDGE=ON \
      -DCMAKE_THREAD_LIBS_INIT="-lpthread" \
      -DCMAKE_HAVE_THREADS_LIBRARY=ON \
      -DCMAKE_USE_PTHREADS_INIT=ON \
      -DTHREADS_PTHREAD_ARG="-pthread" \
      ../
cmake --build . --target aws-iot-device-client -j"$(nproc)"


BIN_SRC="${SRC_DIR}/aws-iot-device-client"
if [ ! -x "${BIN_SRC}" ]; then
  if [ -x "${SRC_DIR}/build/aws-iot-device-client" ]; then
    BIN_SRC="${SRC_DIR}/build/aws-iot-device-client"
  else
    echo "Failed to find built aws-iot-device-client binary" >&2
    exit 1
  fi
fi


PKG_DEBIAN_DIR=${PKG_DIR}/DEBIAN
PKG_ETC_DIR=${PKG_DIR}/etc/${SERVICE_NAME}
PKG_BIN_DIR=${PKG_DIR}/usr/sbin
PKG_LIB_DIR=${PKG_DIR}/usr/lib/${ARCH}-linux-gnu
PKG_LOGROTATE_DIR=${PKG_DIR}/etc/logrotate.d
PKG_SYSTEMD_DIR=${PKG_DIR}/etc/systemd/system
PKG_TMPFILES_DIR=${PKG_DIR}/etc/tmpfiles.d
PKG_VAR_LOG_DIR=${PKG_DIR}/var/log/${SERVICE_NAME}
PKG_DEFAULT_CONF_DIR=${PKG_DIR}/etc/aws-iot-device-client
PKG_ENV_DIR=${PKG_DIR}/etc/default
PKG_LIBEXEC_DIR=${PKG_DIR}/usr/lib/${SERVICE_NAME}
PKG_OPT_DIR=${PKG_DIR}/opt/aws-iot-device-client

mkdir -p \
  "${PKG_DEBIAN_DIR}" \
  "${PKG_ETC_DIR}" \
  "${PKG_BIN_DIR}" \
  "${PKG_LIB_DIR}" \
  "${PKG_LOGROTATE_DIR}" \
  "${PKG_SYSTEMD_DIR}" \
  "${PKG_TMPFILES_DIR}" \
  "${PKG_VAR_LOG_DIR}" \
  "${PKG_DEFAULT_CONF_DIR}" \
  "${PKG_ENV_DIR}" \
  "${PKG_LIBEXEC_DIR}" \
  "${PKG_OPT_DIR}"


cp "${BIN_SRC}" "${PKG_BIN_DIR}/${SERVICE_NAME}"
strip --strip-unneeded "${PKG_BIN_DIR}/${SERVICE_NAME}" || true


SOFTWAREUPDATE_SRC="${ROOT_DIR}/softwareupdate.sh"
if [ -f "${SOFTWAREUPDATE_SRC}" ]; then
  cp "${SOFTWAREUPDATE_SRC}" "${PKG_LIBEXEC_DIR}/softwareupdate.sh"
  chmod 0755 "${PKG_LIBEXEC_DIR}/softwareupdate.sh"
  echo "Packaged softwareupdate.sh job handler"
else
  echo "Warning: softwareupdate.sh not found at ${SOFTWAREUPDATE_SRC}"
fi

LOGDOWNLOAD_SRC="${ROOT_DIR}/logdownload.sh"
if [ -f "${LOGDOWNLOAD_SRC}" ]; then
  cp "${LOGDOWNLOAD_SRC}" "${PKG_LIBEXEC_DIR}/logdownload.sh"
  chmod 0755 "${PKG_LIBEXEC_DIR}/logdownload.sh"
  echo "Packaged logdownload.sh job handler"
else
  echo "Warning: logdownload.sh not found at ${LOGDOWNLOAD_SRC}"
fi


SAMPLE_HANDLERS_SRC="${SRC_DIR}/sample-job-handlers"
PKG_SAMPLE_HANDLERS_DIR="${PKG_LIBEXEC_DIR}/sample-job-handlers"
if [ -d "${SAMPLE_HANDLERS_SRC}" ]; then
  echo "Copying sample job handlers from ${SAMPLE_HANDLERS_SRC}..."
  mkdir -p "${PKG_SAMPLE_HANDLERS_DIR}"
  cp -r "${SAMPLE_HANDLERS_SRC}"/* "${PKG_SAMPLE_HANDLERS_DIR}/"

  find "${PKG_SAMPLE_HANDLERS_DIR}" -type f -name "*.sh" -exec chmod 0755 {} \;
  echo "Packaged sample job handlers"
else
  echo "Warning: sample-job-handlers directory not found at ${SAMPLE_HANDLERS_SRC}"
fi


cat > "${PKG_DEFAULT_CONF_DIR}/aws-iot-device-client.conf" <<'EOF'
{
  "endpoint": "AWS_IOT_ENDPOINT_PLACEHOLDER",
  "cert": "/opt/aws-iot-device-client/certs/claim-certificate.pem.crt",
  "key": "/opt/aws-iot-device-client/certs/claim-private.pem.key",
  "root-ca": "/opt/aws-iot-device-client/certs/AmazonRootCA1.pem",
  "thing-name": "THING_NAME_PLACEHOLDER",
  "fleet-provisioning": {
    "enabled": true,
    "template-name": "FLEET_PROVISIONING_TEMPLATE_PLACEHOLDER",
    "template-parameters": "{\"serialNumber\": \"THING_NAME_PLACEHOLDER\"}"
  },
  "jobs": {
    "enabled": true,
    "handler-directory": "/etc/aws-iot-device-client/jobs"
  },
  "tunneling": {
    "enabled": true
  },
  "device-defender": {
    "enabled": true,
    "interval": 300
  },
  "logging": {
    "level": "INFO",
    "type": "FILE",
    "file": "/var/log/aws-iot-device-client/aws-iot-device-client.log"
  },
  "config-shadow": {
    "enabled": true
  },
  "localMqttBridge": {
    "enabled": true,
    "local": {
      "host": "127.0.0.1",
      "port": 1883,
      "useTLS": false,
      "username": "",
      "password": ""
    },
    "routesFile": "/opt/aws-iot-device-client/gate-local-mqtt-routes.json",
    "routesFileJsonPointer": "/routes",
    "queue": {
      "maxInMemory": 200,
      "dedupeHeartbeat": true
    },
    "loopGuard": {
      "ttlSeconds": 5,
      "maxEntries": 512
    },
    "metrics": {
      "publishIntervalSec": 300,
      "awsTopic": "devices/${thingName}/metrics/local-mqtt-bridge",
      "enabled": true
    }
  }
}
EOF


PKG_NAME=${PKG_NAME:-aws-iot-device-client}
PKG_VERSION=${PKG_VERSION:-1.10.1.1}
cat > "${PKG_DEBIAN_DIR}/control" <<EOF
Package: ${PKG_NAME}
Version: ${PKG_VERSION}
Section: net
Priority: optional
Architecture: ${ARCH}
Maintainer: Venu madhavan Varada <venumadhavan.varada@cubic.com>
Description: AWS IoT Device Client Package
 Device-side app that connects to AWS IoT Core for Jobs, Tunneling, Defender, etc.
Depends: libssl3 | libssl1.1, zlib1g, jq, systemd
EOF


cat > "${PKG_DEBIAN_DIR}/conffiles" <<EOF
/etc/default/aws-iot-device-client
EOF

cat > "${PKG_DEBIAN_DIR}/postinst" <<'EOF'
#!/bin/sh
set -e

SERVICE=aws-iot-device-client
CONF_DIR=/etc/aws-iot-device-client
LOG_DIR=/var/log/${SERVICE}
CLAIM_CERTS_DIR=/opt/aws-iot-device-client/certs
IDENTITY_DIR=/opt/aws-iot-device-client/identity

# Create required directories with proper permissions per AWS documentation
# https://github.com/awslabs/aws-iot-device-client/blob/main/docs/PERMISSIONS.md
install -d -m 0745 ${LOG_DIR}                              # Log directory: 745
install -d -m 0745 /etc/aws-iot-device-client              # Config directory: 745
install -d -m 0700 /etc/aws-iot-device-client/jobs         # Job handlers: 700
install -d -m 0755 /opt/aws-iot-device-client              # Base directory
install -d -m 0700 ${CLAIM_CERTS_DIR}                      # Certificates: 700
install -d -m 0700 ${IDENTITY_DIR}                         # Certificates: 700

# Create log file if missing with proper permissions
LOG_FILE=${LOG_DIR}/aws-iot-device-client.log
if [ ! -f ${LOG_FILE} ]; then
  touch ${LOG_FILE}
  chmod 0600 ${LOG_FILE}                                   # Log file: 600
fi

# Set config file permissions if exists
if [ -f ${CONF_DIR}/aws-iot-device-client.conf ]; then
  chmod 0640 ${CONF_DIR}/aws-iot-device-client.conf        # Config file: 640 (recommended)
fi

# Copy job handler scripts from /usr/lib and ensure they are executable
DMS_TOOLS_DIR="/usr/lib/${SERVICE}"
SOFTWAREUPDATE_SCRIPT="/etc/aws-iot-device-client/jobs/softwareupdate.sh"
LOGDOWNLOAD_SCRIPT="/etc/aws-iot-device-client/jobs/logdownload.sh"

if [ -f "${DMS_TOOLS_DIR}/softwareupdate.sh" ]; then
  cp "${DMS_TOOLS_DIR}/softwareupdate.sh" ${SOFTWAREUPDATE_SCRIPT}
  chmod 0700 ${SOFTWAREUPDATE_SCRIPT}
  echo "Copied and set permissions for softwareupdate.sh"
fi

if [ -f "${DMS_TOOLS_DIR}/logdownload.sh" ]; then
  cp "${DMS_TOOLS_DIR}/logdownload.sh" ${LOGDOWNLOAD_SCRIPT}
  chmod 0700 ${LOGDOWNLOAD_SCRIPT}
  echo "Copied and set permissions for logdownload.sh"
fi

# Copy sample job handlers
SAMPLE_HANDLERS_PKG_DIR="/usr/lib/${SERVICE}/sample-job-handlers"
if [ -d "${SAMPLE_HANDLERS_PKG_DIR}" ]; then
    cp -r ${SAMPLE_HANDLERS_PKG_DIR}/* /etc/aws-iot-device-client/jobs/
    find /etc/aws-iot-device-client/jobs -type f -name "*.sh" -exec chmod 0700 {} \;
    find /etc/aws-iot-device-client/jobs -type f ! -name "*.sh" -exec chmod 0700 {} \;
fi

# Set permissions on certificate files
if [ -f ${CLAIM_CERTS_DIR}/claim-private.pem.key ]; then
  chmod 0600 ${CLAIM_CERTS_DIR}/claim-private.pem.key
fi
if [ -f ${CLAIM_CERTS_DIR}/claim-certificate.pem.crt ]; then
  chmod 0644 ${CLAIM_CERTS_DIR}/claim-certificate.pem.crt
fi
if [ -f ${CLAIM_CERTS_DIR}/AmazonRootCA1.pem ]; then
  chmod 0644 ${CLAIM_CERTS_DIR}/AmazonRootCA1.pem
fi
if [ -f ${IDENTITY_DIR}/active-private.pem.key ]; then
  chmod 0600 ${IDENTITY_DIR}/active-private.pem.key
fi
if [ -f ${IDENTITY_DIR}/active-certificate.pem.crt ]; then
  chmod 0644 ${IDENTITY_DIR}/active-certificate.pem.crt
fi

# Apply systemd tmpfiles and enable service
systemd-tmpfiles --create /etc/tmpfiles.d/${SERVICE}.conf || true

systemctl daemon-reload || true
systemctl enable ${SERVICE} || true
echo "Service enabled. Will start after prerequisites are available."
EOF
chmod 0755 "${PKG_DEBIAN_DIR}/postinst"


cat > "${PKG_DEBIAN_DIR}/prerm" <<'EOF'
#!/bin/sh
set -e
SERVICE=aws-iot-device-client
systemctl stop ${SERVICE} || true
systemctl disable ${SERVICE} || true
EOF
chmod 0755 "${PKG_DEBIAN_DIR}/prerm"


cat > "${PKG_DEBIAN_DIR}/postrm" <<'EOF'
#!/bin/sh
set -e
SERVICE=aws-iot-device-client
# Preserve identity certificates directory on purge.
if [ "$1" = "purge" ]; then
  systemctl daemon-reload || true
fi
EOF
chmod 0755 "${PKG_DEBIAN_DIR}/postrm"


cat > "${PKG_LOGROTATE_DIR}/${SERVICE_NAME}" <<EOF
/var/log/${SERVICE_NAME}/${SERVICE_NAME}.log {
        rotate 7
        daily
        compress
        size 200k
  copytruncate
        nocreate
        missingok
}
EOF


cat > "${PKG_SYSTEMD_DIR}/${SERVICE_NAME}.service" <<'EOF'
[Unit]
Description=AWS IoT Device Client
After=network.target
Wants=network.target

[Service]
Type=simple
User=root
Group=root
EnvironmentFile=-/etc/default/aws-iot-device-client
RuntimeDirectory=aws-iot-device-client
RuntimeDirectoryMode=0745
ExecStartPre=/usr/lib/aws-iot-device-client/wait-for-prerequisites.sh
ExecStartPre=/usr/lib/aws-iot-device-client/gen-config.sh
ExecStart=/usr/sbin/aws-iot-device-client --config-file /run/aws-iot-device-client/aws-iot-device-client.conf
Restart=on-failure
RestartSec=30
SyslogIdentifier=aws-iot-device-client

[Install]
WantedBy=multi-user.target
EOF

cat > "${PKG_TMPFILES_DIR}/${SERVICE_NAME}.conf" <<'EOF'
d /var/log/aws-iot-device-client 0745 root root -
f /var/log/aws-iot-device-client/aws-iot-device-client.log 0600 root root -
d /opt/aws-iot-device-client 0755 root root -
d /opt/aws-iot-device-client/certs 0700 root root -
d /opt/aws-iot-device-client/identity 0700 root root -
d /etc/aws-iot-device-client 0745 root root -
d /etc/aws-iot-device-client/jobs 0700 root root -
EOF

cat > "${PKG_ENV_DIR}/${SERVICE_NAME}" <<'EOF'

#AWS_IOT_ENDPOINT=
#FLEET_PROVISIONING_TEMPLATE=
EOF

cat > "${PKG_LIBEXEC_DIR}/wait-for-prerequisites.sh" <<'EOF'
#!/bin/sh
set -e

SERVICE=aws-iot-device-client
AWS_CONFIG_FILE=/opt/aws-iot-device-client/aws-iot-config.env
CLAIM_CERTS_DIR=/opt/aws-iot-device-client/certs
IDENTITY_DIR=/opt/aws-iot-device-client/identity
CHECK_INTERVAL=30
MQTT_HOST=127.0.0.1
MQTT_PORT=1883
MQTT_TOPIC="status/gate/hardware"

echo "[$SERVICE] Checking for required files..."

elapsed=0
while true; do
  # Check for config file
  if [ ! -f "${AWS_CONFIG_FILE}" ]; then
    echo "[$SERVICE] Waiting for config file: ${AWS_CONFIG_FILE} (waited ${elapsed}s)"
    sleep $CHECK_INTERVAL
    elapsed=$((elapsed + CHECK_INTERVAL))
    continue
  fi

  # Check for claim certificates (identity certs are created automatically after provisioning)
  if [ ! -f "${CLAIM_CERTS_DIR}/claim-certificate.pem.crt" ] || \
     [ ! -f "${CLAIM_CERTS_DIR}/claim-private.pem.key" ] || \
     [ ! -f "${CLAIM_CERTS_DIR}/AmazonRootCA1.pem" ]; then
    echo "[$SERVICE] Waiting for claim certificates in ${CLAIM_CERTS_DIR} (waited ${elapsed}s)"
    sleep $CHECK_INTERVAL
    elapsed=$((elapsed + CHECK_INTERVAL))
    continue
  fi

  # Check if mosquitto is available
  if ! command -v mosquitto_sub >/dev/null 2>&1; then
    echo "[$SERVICE] mosquitto_sub not found. Waiting... (waited ${elapsed}s)"
    sleep $CHECK_INTERVAL
    elapsed=$((elapsed + CHECK_INTERVAL))
    continue
  fi

  # Check if mosquitto broker is running (using timeout and mosquitto_sub)
  if ! timeout 5 mosquitto_sub -h ${MQTT_HOST} -p ${MQTT_PORT} -t '$SYS/broker/version' -C 1 >/dev/null 2>&1; then
    echo "[$SERVICE] Local MQTT broker not available at ${MQTT_HOST}:${MQTT_PORT}. Waiting... (waited ${elapsed}s)"
    sleep $CHECK_INTERVAL
    elapsed=$((elapsed + CHECK_INTERVAL))
    continue
  fi

  # Try to read serial number from MQTT topic
  echo "[$SERVICE] Attempting to read serial number from MQTT topic: ${MQTT_TOPIC}"
  SERIAL_NUMBER=$(timeout 10 mosquitto_sub -h ${MQTT_HOST} -p ${MQTT_PORT} -t ${MQTT_TOPIC} -C 1 2>/dev/null | jq -r '.sbc.serialNumber // empty' 2>/dev/null || echo "")

  if [ -z "${SERIAL_NUMBER}" ]; then
    echo "[$SERVICE] Serial number not available from ${MQTT_TOPIC}. Waiting... (waited ${elapsed}s)"
    sleep $CHECK_INTERVAL
    elapsed=$((elapsed + CHECK_INTERVAL))
    continue
  fi

  echo "[$SERVICE] Serial number retrieved: ${SERIAL_NUMBER}"
  echo "[$SERVICE] All required files and data found. Starting service..."
  exit 0
done
EOF
chmod 0755 "${PKG_LIBEXEC_DIR}/wait-for-prerequisites.sh"


cat > "${PKG_LIBEXEC_DIR}/gen-config.sh" <<'EOF'
#!/bin/sh
set -e

SERVICE=aws-iot-device-client
USER=root
GROUP=root
TEMPLATE=/etc/aws-iot-device-client/aws-iot-device-client.conf
OUT_DIR=/run/${SERVICE}
OUT=${OUT_DIR}/aws-iot-device-client.conf
AWS_CONFIG_FILE=/opt/aws-iot-device-client/aws-iot-config.env
CLAIM_CERTS_DIR=/opt/aws-iot-device-client/certs
IDENTITY_DIR=/opt/aws-iot-device-client/identity
MQTT_HOST=127.0.0.1
MQTT_PORT=1883
MQTT_TOPIC="status/gate/hardware"

install -d -m 0750 -o ${USER} -g ${GROUP} ${OUT_DIR}

if [ ! -f "${TEMPLATE}" ]; then
  echo "Template config not found: ${TEMPLATE}" >&2
  exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "jq is required to generate config" >&2
  exit 1
fi

if [ -f "${AWS_CONFIG_FILE}" ]; then
  while IFS= read -r line; do
    case "$line" in
      ''|'#'*) continue ;;
      *=*) export "$line" ;;
    esac
  done < "${AWS_CONFIG_FILE}"
fi

# Read serial number from MQTT topic
echo "Reading serial number from MQTT topic: ${MQTT_TOPIC}"
SERIAL_NUMBER=$(timeout 10 mosquitto_sub -h ${MQTT_HOST} -p ${MQTT_PORT} -t ${MQTT_TOPIC} -C 1 2>/dev/null | jq -r '.sbc.serialNumber // empty' 2>/dev/null || echo "")

if [ -z "${SERIAL_NUMBER}" ]; then
  echo "Failed to read serial number from MQTT topic ${MQTT_TOPIC}" >&2
  exit 1
fi

echo "AWS_IOT_ENDPOINT=${AWS_IOT_ENDPOINT:-<not set>}"
echo "FLEET_PROVISIONING_TEMPLATE=${FLEET_PROVISIONING_TEMPLATE:-<not set>}"
echo "SERIAL_NUMBER=${SERIAL_NUMBER}"

# Check for certificates in multiple locations
# First check where AWS IoT Device Client actually stores them
AWS_CLIENT_KEYS_DIR="/root/.aws-iot-device-client/keys"
CERT_PATH="${CLAIM_CERTS_DIR}/claim-certificate.pem.crt"
KEY_PATH="${CLAIM_CERTS_DIR}/claim-private.pem.key"

if [ -f "${AWS_CLIENT_KEYS_DIR}/active-private.pem.key" ] && [ -f "${AWS_CLIENT_KEYS_DIR}/active-certificate.pem.crt" ]; then
  CERT_PATH="${AWS_CLIENT_KEYS_DIR}/active-certificate.pem.crt"
  KEY_PATH="${AWS_CLIENT_KEYS_DIR}/active-private.pem.key"
  echo "Using identity certificates from ${AWS_CLIENT_KEYS_DIR}"
elif [ -f "${IDENTITY_DIR}/active-private.pem.key" ] && [ -f "${IDENTITY_DIR}/active-certificate.pem.crt" ]; then
  CERT_PATH="${IDENTITY_DIR}/active-certificate.pem.crt"
  KEY_PATH="${IDENTITY_DIR}/active-private.pem.key"
  echo "Using identity certificates from ${IDENTITY_DIR}"
fi

jq --arg endpoint "${AWS_IOT_ENDPOINT:-}" \
   --arg serial_number "${SERIAL_NUMBER}" \
   --arg template_name "${FLEET_PROVISIONING_TEMPLATE:-}" \
   --arg cert "${CERT_PATH}" \
   --arg key "${KEY_PATH}" '
  .cert = $cert
| .key = $key
| (if $endpoint != "" then .endpoint = $endpoint else . end)
| (if $serial_number != "" then .["thing-name"] = $serial_number else . end)
| (if $template_name != "" then .["fleet-provisioning"]["template-name"] = $template_name else . end)
| (if $serial_number != "" then
    .["fleet-provisioning"]["template-parameters"] = (
      "{\"serialNumber\": \"" + $serial_number + "\"}" )
    else . end)
' "${TEMPLATE}" > "${OUT}.tmp"

chown ${USER}:${GROUP} "${OUT}.tmp"
chmod 0640 "${OUT}.tmp"
mv "${OUT}.tmp" "${OUT}"
EOF
chmod 0755 "${PKG_LIBEXEC_DIR}/gen-config.sh"

chmod 0755 -R "${PKG_DEBIAN_DIR}"

cd "${BUILD_DIR}"
dpkg-deb --build --root-owner-group -Zgzip "${PKG_DIR}" "${SERVICE_NAME}-${PKG_VERSION}.deb"

echo "Package created: ${BUILD_DIR}/${SERVICE_NAME}-${PKG_VERSION}.deb"

#version 1.1.4
