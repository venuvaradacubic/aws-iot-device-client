// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Metrics.h"

using namespace Aws::Iot::DeviceClient::LocalMqttBridge;
using namespace Aws::Crt;

Aws::Crt::String Metrics::toJson(uint32_t offlineDepth, uint32_t localQueueDepth, uint32_t awsQueueDepth) const
{
    JsonObject obj;
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
    obj.WithInteger("forwardedUp", static_cast<int64_t>(forwardedUp.load()));
    obj.WithInteger("forwardedDown", static_cast<int64_t>(forwardedDown.load()));
    obj.WithInteger("throttledMessages", static_cast<int64_t>(throttledMessages.load()));
    obj.WithInteger("droppedLoop", static_cast<int64_t>(droppedLoop.load()));
    obj.WithInteger("droppedQos2", static_cast<int64_t>(droppedQos2.load()));
    obj.WithInteger("queuedOffline", static_cast<int64_t>(queuedOffline.load()));
    obj.WithInteger("publishErrors", static_cast<int64_t>(publishErrors.load()));
    obj.WithInteger("droppedOverflow", static_cast<int64_t>(droppedOverflow.load()));
    obj.WithInteger("offlineBufferDepth", offlineDepth);
    obj.WithInteger("localQueueDepth", localQueueDepth);
    obj.WithInteger("awsQueueDepth", awsQueueDepth);
    obj.WithInteger("uptimeSec", static_cast<int64_t>(uptime));
    obj.WithString("thingName", Aws::Crt::String(thingName.c_str()));
    obj.WithString("version", Aws::Crt::String("1.0"));
    return obj.View().WriteCompact();
}
