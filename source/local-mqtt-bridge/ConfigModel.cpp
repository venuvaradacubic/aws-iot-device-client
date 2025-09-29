// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ConfigModel.h"
#include "../logging/LoggerFactory.h"
#include <regex>

using namespace std;
using namespace Aws::Iot::DeviceClient::LocalMqttBridge;
using namespace Aws::Iot::DeviceClient::Logging;

namespace Aws
{
    namespace Iot
    {
        namespace DeviceClient
        {
            namespace LocalMqttBridge
            {
                static constexpr char TAG[] = "ConfigModel.cpp";

                bool isValidDirection(const std::string& direction)
                {
                    return direction == "up" || direction == "down";
                }

                bool isValidQoS(int qos)
                {
                    return qos >= 0 && qos <= 1;
                }

                std::string expandTopicTemplate(const std::string& template_str, 
                                               const std::string& thingName,
                                               const std::map<std::string, std::string>& variables)
                {
                    std::string result = template_str;
                    
                    // Replace ${thingName} first
                    size_t pos = 0;
                    while ((pos = result.find("${thingName}", pos)) != std::string::npos)
                    {
                        result.replace(pos, 12, thingName);
                        pos += thingName.length();
                    }
                    
                    // Replace other variables
                    for (const auto& var : variables)
                    {
                        std::string placeholder = "${" + var.first + "}";
                        pos = 0;
                        while ((pos = result.find(placeholder, pos)) != std::string::npos)
                        {
                            result.replace(pos, placeholder.length(), var.second);
                            pos += var.second.length();
                        }
                    }
                    
                    return result;
                }

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws