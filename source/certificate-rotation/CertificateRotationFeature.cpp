// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CertificateRotationFeature.h"
#include "../fleetprovisioning/FleetProvisioning.h"
#include "../logging/LoggerFactory.h"

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <random>

#ifndef _WIN32
#    include <signal.h>
#    include <unistd.h>
#endif

using namespace std;
using namespace Aws::Iot::DeviceClient;
using namespace Aws::Iot::DeviceClient::CertificateRotation;
using namespace Aws::Iot::DeviceClient::FleetProvisioningNS;
using namespace Aws::Iot::DeviceClient::Logging;

constexpr char CertificateRotationFeature::TAG[];
constexpr char CertificateRotationFeature::NAME[];

namespace
{
    time_t timegmPortable(tm *timeInfo)
    {
#ifdef _WIN32
        return _mkgmtime(timeInfo);
#else
        return timegm(timeInfo);
#endif
    }
}

int CertificateRotationFeature::init(
    shared_ptr<SharedCrtResourceManager> manager,
    shared_ptr<ClientBaseNotifier> notifier,
    const PlainConfig &config)
{
    resourceManager = manager;
    baseNotifier = notifier;
    configSnapshot = config;
    return Feature::SUCCESS;
}

std::string CertificateRotationFeature::getName()
{
    return NAME;
}

int CertificateRotationFeature::start()
{
    if (running.load())
    {
        return Feature::SUCCESS;
    }

    if (!configSnapshot.certificateRotation.enabled)
    {
        LOG_INFO(TAG, "Certificate Rotation feature is disabled");
        return Feature::SUCCESS;
    }

    running.store(true);
    workerThread.reset(new std::thread(&CertificateRotationFeature::runLoop, this));
    if (baseNotifier)
    {
        baseNotifier->onEvent(this, ClientBaseEventNotification::FEATURE_STARTED);
    }

    return Feature::SUCCESS;
}

int CertificateRotationFeature::stop()
{
    if (!running.load())
    {
        return Feature::SUCCESS;
    }

    running.store(false);
    if (workerThread && workerThread->joinable())
    {
        workerThread->join();
    }
    workerThread.reset();

    if (baseNotifier)
    {
        baseNotifier->onEvent(this, ClientBaseEventNotification::FEATURE_STOPPED);
    }
    return Feature::SUCCESS;
}

void CertificateRotationFeature::sleepInterruptible(int seconds) const
{
    for (int elapsed = 0; running.load() && elapsed < seconds; ++elapsed)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

bool CertificateRotationFeature::readDaysToExpiry(int &daysRemaining) const
{
    if (!configSnapshot.cert.has_value() || configSnapshot.cert->empty())
    {
        LOG_ERROR(TAG, "No certificate path configured for Certificate Rotation");
        return false;
    }

    const std::string certPath = configSnapshot.cert.value();
    FILE *certFile = fopen(certPath.c_str(), "r");
    if (certFile == nullptr)
    {
        LOGM_ERROR(TAG, "Failed to open certificate file for rotation checks: %s", certPath.c_str());
        return false;
    }

    X509 *cert = PEM_read_X509(certFile, nullptr, nullptr, nullptr);
    fclose(certFile);

    if (cert == nullptr)
    {
        LOGM_ERROR(TAG, "Unable to parse certificate file for rotation checks: %s", certPath.c_str());
        return false;
    }

    const ASN1_TIME *notAfter = X509_get0_notAfter(cert);
    if (notAfter == nullptr)
    {
        X509_free(cert);
        LOG_ERROR(TAG, "Unable to read certificate expiration time");
        return false;
    }

    tm expiryTm = {};
    if (ASN1_TIME_to_tm(notAfter, &expiryTm) != 1)
    {
        X509_free(cert);
        LOG_ERROR(TAG, "Unable to convert certificate expiration time");
        return false;
    }

    const time_t expiryEpoch = timegmPortable(&expiryTm);
    X509_free(cert);

    if (expiryEpoch < 0)
    {
        LOG_ERROR(TAG, "Invalid certificate expiration value");
        return false;
    }

    const time_t nowEpoch = std::time(nullptr);
    const double secondsRemaining = std::difftime(expiryEpoch, nowEpoch);
    daysRemaining = static_cast<int>(std::floor(secondsRemaining / (60.0 * 60.0 * 24.0)));

    return true;
}

bool CertificateRotationFeature::shouldAttemptRotation(int daysRemaining) const
{
    return daysRemaining <= configSnapshot.certificateRotation.rotateBeforeExpiryDays;
}

bool CertificateRotationFeature::performRotation()
{
    if (!resourceManager)
    {
        LOG_ERROR(TAG, "SharedCrtResourceManager is null, cannot rotate certificate");
        return false;
    }

    PlainConfig mutableConfig = configSnapshot;
    FleetProvisioning fleetProvisioning;

    LOG_INFO(TAG, "Attempting certificate rotation using Fleet Provisioning");
    if (!fleetProvisioning.ProvisionDevice(resourceManager, mutableConfig))
    {
        LOG_ERROR(TAG, "Certificate rotation via Fleet Provisioning failed");
        return false;
    }

    LOG_INFO(TAG, "Certificate rotation succeeded");
    return true;
}

void CertificateRotationFeature::requestProcessRecycle() const
{
    LOG_INFO(TAG, "Requesting controlled process recycle after certificate rotation");

#ifndef _WIN32
    if (::kill(::getpid(), SIGTERM) != 0)
    {
        LOG_WARN(TAG, "Failed to send SIGTERM for recycle, exiting process directly");
        std::exit(EXIT_SUCCESS);
    }
#else
    std::exit(EXIT_SUCCESS);
#endif
}

void CertificateRotationFeature::runLoop()
{
    const int startupJitter = configSnapshot.certificateRotation.startupJitterSeconds;
    if (startupJitter > 0)
    {
        std::mt19937 rng(static_cast<unsigned int>(std::time(nullptr)));
        std::uniform_int_distribution<int> distribution(0, startupJitter);
        const int jitterDelay = distribution(rng);
        LOGM_INFO(TAG, "Certificate Rotation startup jitter delay: %d seconds", jitterDelay);
        sleepInterruptible(jitterDelay);
    }

    while (running.load())
    {
        int daysRemaining = 0;
        int waitSeconds = configSnapshot.certificateRotation.checkIntervalSeconds;

        if (!readDaysToExpiry(daysRemaining))
        {
            waitSeconds = configSnapshot.certificateRotation.failureBackoffSeconds;
        }
        else
        {
            LOGM_INFO(TAG, "Current certificate expires in %d day(s)", daysRemaining);

            if (shouldAttemptRotation(daysRemaining))
            {
                const auto now = std::chrono::steady_clock::now();
                const int cooldownSeconds = configSnapshot.certificateRotation.failureBackoffSeconds;

                if (lastFailureTimestamp != std::chrono::steady_clock::time_point::min())
                {
                    const auto elapsed =
                        std::chrono::duration_cast<std::chrono::seconds>(now - lastFailureTimestamp).count();
                    if (elapsed < cooldownSeconds)
                    {
                        waitSeconds = cooldownSeconds - static_cast<int>(elapsed);
                        LOGM_INFO(
                            TAG,
                            "Skipping rotation attempt due to cooldown. Next attempt in %d second(s)",
                            waitSeconds);
                        sleepInterruptible(waitSeconds);
                        continue;
                    }
                }

                if (performRotation())
                {
                    requestProcessRecycle();
                    return;
                }

                lastFailureTimestamp = now;
                waitSeconds = cooldownSeconds;
            }
        }

        sleepInterruptible(waitSeconds);
    }
}
