// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_ROUTE_MATCHER_H
#define AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_ROUTE_MATCHER_H

#include "ConfigModel.h"
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <regex>

namespace Aws
{
    namespace Iot
    {
        namespace DeviceClient
        {
            namespace LocalMqttBridge
            {
                /**
                 * @brief Result of a topic matching operation
                 */
                struct MatchResult 
                {
                    const Route* route;
                    std::map<std::string, std::string> variables; // ${match1}, ${tail}, etc.
                    
                    MatchResult(const Route* r) : route(r) {}
                    MatchResult(const Route* r, const std::map<std::string, std::string>& vars) 
                        : route(r), variables(vars) {}
                };

                /**
                 * @brief Handles topic matching and variable extraction for bridge routing
                 */
                class RouteMatcher
                {
                public:
                    RouteMatcher() = default;
                    ~RouteMatcher() = default;

                    /**
                     * @brief Initialize the matcher with routes and thing name
                     * @param routes List of routes to configure
                     * @param thingName AWS IoT thing name for variable expansion
                     */
                    void addRoutes(const std::vector<Route>& routes, const std::string& thingName);

                    /**
                     * @brief Find up route for exact local topic match
                     * @param localTopic The local topic to match against up routes
                     * @return Pointer to matching route or nullptr if no match
                     */
                    const Route* matchUp(const std::string& localTopic) const;

                    /**
                     * @brief Find down route for AWS topic with wildcard matching
                     * @param awsTopic The AWS topic to match against down route patterns
                     * @return MatchResult with route and extracted variables, or nullptr route if no match
                     */
                    std::unique_ptr<MatchResult> matchDown(const std::string& awsTopic) const;

                    /**
                     * @brief Generate local topic from template and variables
                     * @param route The route containing the local topic template
                     * @param variables Map of variable names to values for expansion
                     * @return Expanded local topic string
                     */
                    std::string generateLocalTopic(const Route* route, 
                                                  const std::map<std::string, std::string>& variables) const;

                private:
                    struct CompiledRoute
                    {
                        Route route;
                        std::regex pattern;
                        std::vector<std::string> captureNames; // Names of captured variables
                        
                        CompiledRoute(const Route& r) : route(r) {}
                    };

                    std::vector<Route> upRoutes;
                    std::vector<CompiledRoute> downRoutes; // Compiled patterns for AWS topics
                    std::string thingName;

                    /**
                     * @brief Compile AWS topic pattern into regex with capture groups
                     * @param awsTopic AWS topic pattern with + and # wildcards
                     * @param captureNames Output vector to store capture group names
                     * @return Compiled regex pattern
                     */
                    std::regex compileAwsTopicPattern(const std::string& awsTopic, 
                                                     std::vector<std::string>& captureNames) const;

                    /**
                     * @brief Check if topic matches pattern exactly (for up routes)
                     * @param topic Topic to check
                     * @param pattern Pattern to match against (may contain + wildcards)
                     * @return true if topic matches pattern
                     */
                    bool matchesExact(const std::string& topic, const std::string& pattern) const;
                };

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws

#endif // AWS_IOT_DEVICE_CLIENT_LOCAL_MQTT_BRIDGE_ROUTE_MATCHER_H