// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef DEVICE_CLIENT_CERTIFICATEROTATIONFEATURE_H
#define DEVICE_CLIENT_CERTIFICATEROTATIONFEATURE_H

#include "../ClientBaseNotifier.h"
#include "../Feature.h"
#include "../SharedCrtResourceManager.h"
#include "../config/Config.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace Aws
{
    namespace Iot
    {
        namespace DeviceClient
        {
            namespace CertificateRotation
            {
                class CertificateRotationFeature : public Feature
                {
                  public:
                    static constexpr char NAME[] = "Certificate Rotation";

                    int init(
                        std::shared_ptr<SharedCrtResourceManager> manager,
                        std::shared_ptr<ClientBaseNotifier> notifier,
                        const PlainConfig &config);

                    std::string getName() override;
                    int start() override;
                    int stop() override;

                  private:
                    static constexpr char TAG[] = "CertificateRotationFeature.cpp";

                    std::shared_ptr<SharedCrtResourceManager> resourceManager;
                    std::shared_ptr<ClientBaseNotifier> baseNotifier;
                    PlainConfig configSnapshot;

                    std::atomic<bool> running{false};
                    std::unique_ptr<std::thread> workerThread;

                    std::chrono::steady_clock::time_point lastFailureTimestamp{
                        std::chrono::steady_clock::time_point::min()};

                    void runLoop();
                    void sleepInterruptible(int seconds) const;
                    bool readDaysToExpiry(int &daysRemaining) const;
                    bool shouldAttemptRotation(int daysRemaining) const;
                    bool performRotation();
                    void requestProcessRecycle() const;
                };
            } // namespace CertificateRotation
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws

#endif // DEVICE_CLIENT_CERTIFICATEROTATIONFEATURE_H
