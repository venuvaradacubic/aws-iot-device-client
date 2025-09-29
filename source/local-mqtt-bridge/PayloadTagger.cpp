// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "PayloadTagger.h"
#include "../logging/LoggerFactory.h"
#include <aws/crt/JsonObject.h>

using namespace std;
using namespace Aws::Iot::DeviceClient::LocalMqttBridge;
using namespace Aws::Iot::DeviceClient::Logging;
using namespace Aws::Crt;

namespace Aws
{
    namespace Iot
    {
        namespace DeviceClient
        {
            namespace LocalMqttBridge
            {
                static constexpr char TAG[] = "PayloadTagger.cpp";
                static constexpr char BRIDGE_FIELD[] = "_bridge";

                std::string PayloadTagger::tagPayload(const std::string& payload, const std::string& direction)
                {
                    if (!isValidJson(payload))
                    {
                        // Non-JSON payload, return as-is
                        LOGM_DEBUG(TAG, "Payload is not JSON, skipping tagging");
                        return payload;
                    }

                    if (isTagged(payload))
                    {
                        // Already tagged, don't modify
                        LOGM_DEBUG(TAG, "Payload already tagged with bridge direction");
                        return payload;
                    }

                    try
                    {
                        JsonObject jsonObject(payload);
                        if (!jsonObject.WasParseSuccessful())
                        {
                            LOGM_DEBUG(TAG, "Failed to parse JSON payload for tagging");
                            return payload;
                        }

                        // Add bridge direction tag
                        jsonObject.WithString(BRIDGE_FIELD, direction);
                        
                        std::string taggedPayload = jsonObject.View().WriteCompact();
                        LOGM_DEBUG(TAG, "Tagged payload with direction: %s", direction.c_str());
                        return taggedPayload;
                    }
                    catch (const std::exception& e)
                    {
                        LOGM_WARN(TAG, "Exception while tagging payload: %s", e.what());
                        return payload;
                    }
                }

                bool PayloadTagger::isTagged(const std::string& payload)
                {
                    if (!isValidJson(payload))
                    {
                        return false;
                    }

                    try
                    {
                        JsonObject jsonObject(payload);
                        if (!jsonObject.WasParseSuccessful())
                        {
                            return false;
                        }

                        return jsonObject.View().ValueExists(BRIDGE_FIELD);
                    }
                    catch (const std::exception& e)
                    {
                        LOGM_DEBUG(TAG, "Exception while checking if payload is tagged: %s", e.what());
                        return false;
                    }
                }

                std::string PayloadTagger::getBridgeDirection(const std::string& payload)
                {
                    if (!isValidJson(payload))
                    {
                        return "";
                    }

                    try
                    {
                        JsonObject jsonObject(payload);
                        if (!jsonObject.WasParseSuccessful())
                        {
                            return "";
                        }

                        JsonView view = jsonObject.View();
                        if (view.ValueExists(BRIDGE_FIELD))
                        {
                            return view.GetString(BRIDGE_FIELD).c_str();
                        }
                    }
                    catch (const std::exception& e)
                    {
                        LOGM_DEBUG(TAG, "Exception while getting bridge direction: %s", e.what());
                    }

                    return "";
                }

                bool PayloadTagger::isValidJson(const std::string& payload)
                {
                    if (payload.empty())
                    {
                        return false;
                    }

                    // Quick check for obvious non-JSON strings
                    std::string trimmed = payload;
                    trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
                    trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);

                    if (trimmed.empty() || (trimmed[0] != '{' && trimmed[0] != '['))
                    {
                        return false;
                    }

                    // Try to parse as JSON
                    try
                    {
                        JsonObject jsonObject(payload);
                        return jsonObject.WasParseSuccessful();
                    }
                    catch (const std::exception&)
                    {
                        return false;
                    }
                }

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws