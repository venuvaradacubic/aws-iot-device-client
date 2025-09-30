#!/bin/bash

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
PKG_DEFAULT_CONF_DIR=${PKG_DIR}/etc/.aws-iot-device-client
PKG_ENV_DIR=${PKG_DIR}/etc/default
PKG_LIBEXEC_DIR=${PKG_DIR}/usr/lib/${SERVICE_NAME}
PKG_OPT_FGATE_DIR=${PKG_DIR}/opt/fgate/aws-iot-device-client

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
  "${PKG_OPT_FGATE_DIR}"


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
  "cert": "/opt/fgate/aws-iot-device-client/certs/claim-certificate.pem.crt",
  "key": "/opt/fgate/aws-iot-device-client/certs/claim-private.pem.key",
  "root-ca": "/opt/fgate/aws-iot-device-client/certs/AmazonRootCA1.pem",
  "thing-name": "THING_NAME_PLACEHOLDER",
  "fleet-provisioning": {
    "enabled": true,
    "template-name": "FLEET_PROVISIONING_TEMPLATE_PLACEHOLDER",
    "template-parameters": "{\"deviceId\": \"THING_NAME_PLACEHOLDER\", \"gateType\": \"GATE_TYPE_PLACEHOLDER\", \"gateId\": \"FLARE_GATE_ID_PLACEHOLDER\"}"
  },
  "jobs": {
    "enabled": true,
    "handler-directory": "/etc/.aws-iot-device-client/jobs"
  },
  "tunneling": {
    "enabled": true
  },
  "device-defender": {
    "enabled": true,
    "interval": 300
  },
  "logging": {
    "level": "DEBUG",
    "type": "FILE",
    "file": "/var/log/aws-iot-device-client/aws-iot-device-client.log"
  },
  "samples": {
    "pub-sub": {
      "enabled": true,
      "publish-topic": "config-paddle-force",
  "publish-file": "/var/lib/aws-iot-device-client/pubsub/publish-file.txt",
      "subscribe-topic": "config-paddle-force",
  "subscribe-file": "/var/lib/aws-iot-device-client/pubsub/subscribe-file.txt"
    }
  },
  "config-shadow": {
    "enabled": true
  },
  "sample-shadow": {
    "enabled": true,
    "shadow-name": "gate-controller",
    "shadow-input-file": "/opt/fgate/aws-iot-device-client/shadow/device-shadow-input.json",
    "shadow-output-file": "/opt/fgate/aws-iot-device-client/shadow/device-shadow-output.json"
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
    "routes": [
    {
        "direction": "up",
        "localTopic": "config/gate/paddleForces",
        "awsTopic": "devices/${thingName}/config/gate/paddleForces",
        "qos": 1,
        "throttleSeconds": 0
      },{
        "direction": "down",
        "awsTopic": "devices/${thingName}/status/gate/paddleForces",
        "localTopicTemplate": "status/gate/paddleForces",
        "qos": 0
      }],
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

cat > "${PKG_OPT_FGATE_DIR}/aws-iot-config.env.example" <<'EOF'
# AWS IoT Device Client Configuration (Example)
# Copy to aws-iot-config.env and uncomment values to activate.

#AWS_IOT_ENDPOINT=your-endpoint.iot.region.amazonaws.com
#THING_NAME=your-device-thing-name
#FLEET_PROVISIONING_TEMPLATE=your-fleet-provisioning-template-name
#GATE_TYPE=your-gate-type
#FLARE_GATE_ID=your-gate-id
EOF

PKG_NAME=${PKG_NAME:-aws-iot-device-client-fgate}
PKG_VERSION=${PKG_VERSION:-v1.10.1}
cat > "${PKG_DEBIAN_DIR}/control" <<EOF
Package: ${PKG_NAME}
Version: ${PKG_VERSION}
Section: net
Priority: optional
Architecture: ${ARCH}
Maintainer: Venu madhavan Varada <venumadhavan.varada@cubic.com>
Description: AWS IoT Device Client packaged for Field Gate
 Device-side app that connects to AWS IoT Core for Jobs, Tunneling, Defender, etc.
Depends: libssl3 | libssl1.1, zlib1g, jq, systemd
EOF


cat > "${PKG_DEBIAN_DIR}/conffiles" <<EOF
/etc/.aws-iot-device-client/aws-iot-device-client.conf
/etc/default/aws-iot-device-client
EOF

cat > "${PKG_DEBIAN_DIR}/postinst" <<'EOF'
#!/bin/sh
set -e

SERVICE=aws-iot-device-client
CONF_DIR=/etc/.aws-iot-device-client
LOG_DIR=/var/log/${SERVICE}
USER=root
GROUP=root
CLAIM_CERTS_DIR=/opt/fgate/aws-iot-device-client/certs
IDENTITY_DIR=/opt/fgate/aws-iot-device-client/identity
LEGACY_KEYS_DIR=/home/${USER}/.aws-iot-device-client/keys

if ! getent group ${GROUP} >/dev/null; then
    addgroup --quiet --system ${GROUP}
fi
if ! getent passwd ${USER} >/dev/null; then
    adduser --quiet --system --ingroup ${GROUP} --home /home/${USER} --disabled-login ${USER}
fi

install -d -m 0700 -o ${USER} -g ${GROUP} /home/${USER}
install -d -m 0700 -o ${USER} -g ${GROUP} /home/${USER}/.aws-iot-device-client
install -d -m 0700 -o ${USER} -g ${GROUP} ${LEGACY_KEYS_DIR}


install -d -m 0745 -o ${USER} -g ${GROUP} ${LOG_DIR}
install -d -m 0745 -o ${USER} -g ${GROUP} /etc/.aws-iot-device-client
install -d -m 0700 -o ${USER} -g ${GROUP} /etc/.aws-iot-device-client/jobs

install -d -m 0700 -o ${USER} -g ${GROUP} /opt/fgate
install -d -m 0700 -o ${USER} -g ${GROUP} /opt/fgate/aws-iot-device-client
install -d -m 0700 -o ${USER} -g ${GROUP} ${CLAIM_CERTS_DIR}
install -d -m 0700 -o ${USER} -g ${GROUP} ${IDENTITY_DIR}

SHADOW_DIR=/opt/fgate/aws-iot-device-client/shadow
install -d -m 0700 -o ${USER} -g ${GROUP} ${SHADOW_DIR}
STATE_DIR=/var/lib/aws-iot-device-client
PUBSUB_DIR=${STATE_DIR}/pubsub
install -d -m 0745 -o ${USER} -g ${GROUP} ${STATE_DIR}
install -d -m 0745 -o ${USER} -g ${GROUP} ${PUBSUB_DIR}

LOG_FILE=${LOG_DIR}/aws-iot-device-client.log
if [ ! -f ${LOG_FILE} ]; then
  install -m 0600 -o ${USER} -g ${GROUP} /dev/null ${LOG_FILE}
else
  chown ${USER}:${GROUP} ${LOG_FILE} || true
  chmod 0600 ${LOG_FILE} || true
fi

if [ -f ${CONF_DIR}/aws-iot-device-client.conf ]; then
  chown ${USER}:${GROUP} ${CONF_DIR}/aws-iot-device-client.conf || true
  chmod 0640 ${CONF_DIR}/aws-iot-device-client.conf || true
fi

SOFTWAREUPDATE_SCRIPT="/etc/.aws-iot-device-client/jobs/softwareupdate.sh"
if [ ! -f ${SOFTWAREUPDATE_SCRIPT} ]; then
  if [ -f /usr/lib/${SERVICE}/softwareupdate.sh ]; then
    cp /usr/lib/${SERVICE}/softwareupdate.sh ${SOFTWAREUPDATE_SCRIPT}
    chown ${USER}:${GROUP} ${SOFTWAREUPDATE_SCRIPT}
    chmod 0755 ${SOFTWAREUPDATE_SCRIPT}
  fi
fi

SAMPLE_HANDLERS_PKG_DIR="/usr/lib/${SERVICE}/sample-job-handlers"
if [ -d "${SAMPLE_HANDLERS_PKG_DIR}" ]; then
    cp -r ${SAMPLE_HANDLERS_PKG_DIR}/* /etc/.aws-iot-device-client/jobs/
    find /etc/.aws-iot-device-client/jobs -type f -exec chown ${USER}:${GROUP} {} \; || true
    find /etc/.aws-iot-device-client/jobs -type f -name "*.sh" -exec chmod 0755 {} \; || true
    find /etc/.aws-iot-device-client/jobs -type f ! -name "*.sh" -exec chmod 0700 {} \; || true
fi

# Job handler script permissions - 700 (required for most scripts, 755 for shell scripts)
if [ -d /etc/.aws-iot-device-client/jobs ]; then
  find /etc/.aws-iot-device-client/jobs -maxdepth 1 -type f -exec chown ${USER}:${GROUP} {} \; || true
  # Set executable permissions for job handlers
  find /etc/.aws-iot-device-client/jobs -maxdepth 1 -type f -name "*.sh" -exec chmod 0755 {} \; || true
  # Set secure permissions for other job files
  find /etc/.aws-iot-device-client/jobs -maxdepth 1 -type f ! -name "*.sh" -exec chmod 0700 {} \; || true
fi

# Ensure AWS IoT configuration file exists (copy from example if missing)
AWS_CONFIG_FILE=/opt/fgate/aws-iot-device-client/aws-iot-config.env
AWS_CONFIG_EXAMPLE=/opt/fgate/aws-iot-device-client/aws-iot-config.env.example
if [ ! -f ${AWS_CONFIG_FILE} ] && [ -f ${AWS_CONFIG_EXAMPLE} ]; then
  cp ${AWS_CONFIG_EXAMPLE} ${AWS_CONFIG_FILE}
  echo "Created ${AWS_CONFIG_FILE} from example template" || true
fi
if [ -f ${AWS_CONFIG_FILE} ]; then
  chown ${USER}:${GROUP} ${AWS_CONFIG_FILE} || true
  chmod 0640 ${AWS_CONFIG_FILE} || true
fi

if [ ! -f ${SHADOW_DIR}/device-shadow-input.json ]; then
  cat > ${SHADOW_DIR}/device-shadow-input.json <<'JSON'
{"state":{"reported":{}}}
JSON
  chown ${USER}:${GROUP} ${SHADOW_DIR}/device-shadow-input.json || true
  chmod 0600 ${SHADOW_DIR}/device-shadow-input.json || true
fi
if [ ! -f ${SHADOW_DIR}/device-shadow-output.json ]; then
  install -m 0600 -o ${USER} -g ${GROUP} /dev/null ${SHADOW_DIR}/device-shadow-output.json || true
fi
if [ -f ${SHADOW_DIR}/device-shadow-input.json ]; then 
  chown ${USER}:${GROUP} ${SHADOW_DIR}/device-shadow-input.json || true
  chmod 0600 ${SHADOW_DIR}/device-shadow-input.json || true
fi
if [ -f ${SHADOW_DIR}/device-shadow-output.json ]; then 
  chown ${USER}:${GROUP} ${SHADOW_DIR}/device-shadow-output.json || true
  chmod 0600 ${SHADOW_DIR}/device-shadow-output.json || true
fi

PUBLISH_FILE=${PUBSUB_DIR}/publish-file.txt
SUBSCRIBE_FILE=${PUBSUB_DIR}/subscribe-file.txt
if [ ! -f ${PUBLISH_FILE} ]; then
  install -m 0600 -o ${USER} -g ${GROUP} /dev/null ${PUBLISH_FILE} || true
fi
if [ ! -f ${SUBSCRIBE_FILE} ]; then
  install -m 0600 -o ${USER} -g ${GROUP} /dev/null ${SUBSCRIBE_FILE} || true
fi
chown ${USER}:${GROUP} ${PUBLISH_FILE} ${SUBSCRIBE_FILE} 2>/dev/null || true
chmod 0600 ${PUBLISH_FILE} ${SUBSCRIBE_FILE} 2>/dev/null || true

if [ -d /run/${SERVICE} ]; then chmod 0745 /run/${SERVICE} || true; fi
if [ -f ${LEGACY_KEYS_DIR}/active-private.pem.key ] && [ ! -f ${IDENTITY_DIR}/active-private.pem.key ]; then
  mv ${LEGACY_KEYS_DIR}/active-private.pem.key ${IDENTITY_DIR}/ 2>/dev/null || true
fi
if [ -f ${LEGACY_KEYS_DIR}/active-certificate.pem.crt ] && [ ! -f ${IDENTITY_DIR}/active-certificate.pem.crt ]; then
  mv ${LEGACY_KEYS_DIR}/active-certificate.pem.crt ${IDENTITY_DIR}/ 2>/dev/null || true
fi

if [ -f ${CLAIM_CERTS_DIR}/claim-private.pem.key ]; then
  chown ${USER}:${GROUP} ${CLAIM_CERTS_DIR}/claim-private.pem.key || true
  chmod 0600 ${CLAIM_CERTS_DIR}/claim-private.pem.key || true
fi
if [ -f ${CLAIM_CERTS_DIR}/claim-certificate.pem.crt ]; then
  chown ${USER}:${GROUP} ${CLAIM_CERTS_DIR}/claim-certificate.pem.crt || true
  chmod 0644 ${CLAIM_CERTS_DIR}/claim-certificate.pem.crt || true
fi
if [ -f ${CLAIM_CERTS_DIR}/AmazonRootCA1.pem ]; then
  chown ${USER}:${GROUP} ${CLAIM_CERTS_DIR}/AmazonRootCA1.pem || true
  chmod 0644 ${CLAIM_CERTS_DIR}/AmazonRootCA1.pem || true
fi

if [ -f ${IDENTITY_DIR}/active-private.pem.key ]; then
  chown ${USER}:${GROUP} ${IDENTITY_DIR}/active-private.pem.key || true
  chmod 0600 ${IDENTITY_DIR}/active-private.pem.key || true
fi
if [ -f ${IDENTITY_DIR}/active-certificate.pem.crt ]; then
  chown ${USER}:${GROUP} ${IDENTITY_DIR}/active-certificate.pem.crt || true
  chmod 0644 ${IDENTITY_DIR}/active-certificate.pem.crt || true
fi

systemd-tmpfiles --create /etc/tmpfiles.d/${SERVICE}.conf || true

systemctl daemon-reexec || true
systemctl enable ${SERVICE} || true
systemctl restart ${SERVICE} || true
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
ExecStartPre=/usr/lib/aws-iot-device-client/gen-config.sh
ExecStart=/usr/sbin/aws-iot-device-client --config-file /run/aws-iot-device-client/aws-iot-device-client.conf
Restart=on-failure
SyslogIdentifier=aws-iot-device-client


[Install]
WantedBy=multi-user.target
EOF

cat > "${PKG_TMPFILES_DIR}/${SERVICE_NAME}.conf" <<'EOF'
d /var/log/aws-iot-device-client 0745 root root -
f /var/log/aws-iot-device-client/aws-iot-device-client.log 0600 root root -
d /var/lib/aws-iot-device-client 0745 root root -
d /var/lib/aws-iot-device-client/pubsub 0745 root root -
d /opt/fgate 0755 root root -
d /opt/fgate/aws-iot-device-client 0755 root root -
d /opt/fgate/aws-iot-device-client/certs 0700 root root -
d /opt/fgate/aws-iot-device-client/shadow 0700 root root -
d /opt/fgate/aws-iot-device-client/identity 0700 root root -
d /etc/.aws-iot-device-client 0745 root root -
d /etc/.aws-iot-device-client/jobs 0700 root root -
EOF

cat > "${PKG_ENV_DIR}/${SERVICE_NAME}" <<'EOF'

#AWS_IOT_ENDPOINT=
#THING_NAME=
#FLEET_PROVISIONING_TEMPLATE=
#GATE_TYPE=
#FLARE_GATE_ID=
EOF

cat > "${PKG_LIBEXEC_DIR}/gen-config.sh" <<'EOF'
#!/bin/sh
set -e

SERVICE=aws-iot-device-client
USER=root
GROUP=root
TEMPLATE=/etc/.aws-iot-device-client/aws-iot-device-client.conf
OUT_DIR=/run/${SERVICE}
OUT=${OUT_DIR}/aws-iot-device-client.conf
AWS_CONFIG_FILE=/opt/fgate/aws-iot-device-client/aws-iot-config.env
CLAIM_CERTS_DIR=/opt/fgate/aws-iot-device-client/certs
IDENTITY_DIR=/opt/fgate/aws-iot-device-client/identity

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

fi

echo "AWS_IOT_ENDPOINT=${AWS_IOT_ENDPOINT:-<not set>}"
echo "THING_NAME=${THING_NAME:-<not set>}"
echo "FLEET_PROVISIONING_TEMPLATE=${FLEET_PROVISIONING_TEMPLATE:-<not set>}"
echo "GATE_TYPE=${GATE_TYPE:-<not set>}"
echo "FLARE_GATE_ID=${FLARE_GATE_ID:-<not set>}"
CERT_PATH="${CLAIM_CERTS_DIR}/claim-certificate.pem.crt"
KEY_PATH="${CLAIM_CERTS_DIR}/claim-private.pem.key"
if [ -f "${IDENTITY_DIR}/active-private.pem.key" ] && [ -f "${IDENTITY_DIR}/active-certificate.pem.crt" ]; then
  CERT_PATH="${IDENTITY_DIR}/active-certificate.pem.crt"
  KEY_PATH="${IDENTITY_DIR}/active-private.pem.key"
fi

jq --arg endpoint "${AWS_IOT_ENDPOINT:-}" \
   --arg thing_name "${THING_NAME:-}" \
   --arg template_name "${FLEET_PROVISIONING_TEMPLATE:-}" \
   --arg gate_type "${GATE_TYPE:-}" \
   --arg gate_id "${FLARE_GATE_ID:-}" \
   --arg cert "${CERT_PATH}" \
   --arg key "${KEY_PATH}" '
  .cert = $cert
| .key = $key
| (if $endpoint != "" then .endpoint = $endpoint else . end)
| (if $thing_name != "" then .["thing-name"] = $thing_name else . end)
| (if $template_name != "" then .["fleet-provisioning"]["template-name"] = $template_name else . end)
| (if ($thing_name != "" or $gate_type != "" or $gate_id != "") then
    .["fleet-provisioning"]["template-parameters"] = (
      .["fleet-provisioning"]["template-parameters"]
      | fromjson
      | (if $thing_name != "" then .deviceId = $thing_name else . end)
      | (if $gate_type  != "" then .gateType = $gate_type else . end)
      | (if $gate_id    != "" then .gateId   = $gate_id else . end)
      | tostring )
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
