// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RouteMatcher.h"
#include "../logging/LoggerFactory.h"
#include <sstream>
#include <algorithm>

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
                static constexpr char TAG[] = "RouteMatcher.cpp";

                void RouteMatcher::addRoutes(const std::vector<Route>& routes, const std::string& thingName)
                {
                    this->thingName = thingName;
                    upRoutes.clear();
                    upCompiled.clear();
                    downRoutes.clear();

                    for (const auto& route : routes)
                    {
                        if (route.direction == "up")
                        {
                            upRoutes.push_back(route);
                            // Build compiled pattern if contains '+' for variable capture
                            if (route.localTopic.find('+') != std::string::npos)
                            {
                                CompiledRoute compiled(route);
                                try
                                {
                                    compiled.pattern = compileLocalTopicPattern(route.localTopic, compiled.captureNames);
                                    upCompiled.push_back(std::move(compiled));
                                    LOGM_DEBUG(TAG, "Compiled up route wildcard pattern for local topic: %s", route.localTopic.c_str());
                                }
                                catch(const std::exception& e)
                                {
                                    LOGM_WARN(TAG, "Failed to compile up route pattern %s: %s", route.localTopic.c_str(), e.what());
                                }
                            }
                        }
                        else if (route.direction == "down")
                        {
                            CompiledRoute compiledRoute(route);
                            try 
                            {
                                compiledRoute.pattern = compileAwsTopicPattern(route.awsTopic, compiledRoute.captureNames);
                                downRoutes.push_back(std::move(compiledRoute));
                                LOGM_DEBUG(TAG, "Compiled down route pattern for AWS topic: %s", route.awsTopic.c_str());
                            }
                            catch (const std::exception& e)
                            {
                                LOGM_ERROR(TAG, "Failed to compile route pattern for %s: %s", route.awsTopic.c_str(), e.what());
                            }
                        }
                    }

                    LOGM_INFO(TAG, "Loaded %zu up routes and %zu down routes", upRoutes.size(), downRoutes.size());
                }

                const Route* RouteMatcher::matchUp(const std::string& localTopic) const
                {
                    for (const auto& route : upRoutes)
                    {
                        if (matchesExact(localTopic, route.localTopic))
                        {
                            return &route;
                        }
                    }
                    return nullptr;
                }

                std::unique_ptr<MatchResult> RouteMatcher::matchUpWithVariables(const std::string& localTopic) const
                {
                    // First attempt simple exact (including '+' wildcard matching) without capture
                    const Route* base = matchUp(localTopic);
                    if (base && base->localTopic.find('+') == std::string::npos)
                    {
                        return std::unique_ptr<MatchResult>(new MatchResult(base));
                    }
                    // Try compiled wildcard patterns for capture
                    for (const auto &compiledRoute : upCompiled)
                    {
                        std::smatch matches;
                        if (std::regex_match(localTopic, matches, compiledRoute.pattern))
                        {
                            std::map<std::string, std::string> vars;
                            for (size_t i = 1; i < matches.size() && (i - 1) < compiledRoute.captureNames.size(); ++i)
                            {
                                vars[compiledRoute.captureNames[i-1]] = matches[i].str();
                            }
                            return std::unique_ptr<MatchResult>(new MatchResult(&compiledRoute.route, vars));
                        }
                    }
                    return std::unique_ptr<MatchResult>(new MatchResult(nullptr));
                }

                std::unique_ptr<MatchResult> RouteMatcher::matchDown(const std::string& awsTopic) const
                {
                    for (const auto& compiledRoute : downRoutes)
                    {
                        std::smatch matches;
                        if (std::regex_match(awsTopic, matches, compiledRoute.pattern))
                        {
                            std::map<std::string, std::string> variables;
                            
                            // Extract captured variables
                            for (size_t i = 1; i < matches.size() && (i - 1) < compiledRoute.captureNames.size(); ++i)
                            {
                                const std::string& varName = compiledRoute.captureNames[i - 1];
                                variables[varName] = matches[i].str();
                            }
                            
                            return std::unique_ptr<MatchResult>(new MatchResult(&compiledRoute.route, variables));
                        }
                    }
                    return std::unique_ptr<MatchResult>(new MatchResult(nullptr));
                }

                std::string RouteMatcher::generateLocalTopic(const Route* route, 
                                                           const std::map<std::string, std::string>& variables) const
                {
                    if (!route || route->localTopicTemplate.empty())
                    {
                        return "";
                    }
                    
                    return expandTopicTemplate(route->localTopicTemplate, thingName, variables);
                }

                std::regex RouteMatcher::compileAwsTopicPattern(const std::string& awsTopic, 
                                                              std::vector<std::string>& captureNames) const
                {
                    captureNames.clear();
                    std::string pattern = awsTopic;
                    
                    // First expand ${thingName} placeholder
                    size_t pos = 0;
                    while ((pos = pattern.find("${thingName}", pos)) != std::string::npos)
                    {
                        pattern.replace(pos, 12, thingName);
                        pos += thingName.length();
                    }
                    
                    // Escape regex special characters except + and #
                    std::string escapedPattern;
                    int matchCount = 0;
                    
                    for (size_t i = 0; i < pattern.length(); ++i)
                    {
                        char c = pattern[i];
                        if (c == '+')
                        {
                            // Single level wildcard -> capture group
                            matchCount++;
                            captureNames.push_back("match" + std::to_string(matchCount));
                            escapedPattern += "([^/]+)";
                        }
                        else if (c == '#')
                        {
                            // Multi-level wildcard -> capture remaining as "tail"
                            captureNames.push_back("tail");
                            escapedPattern += "(.*)";
                        }
                        else if (c == '.' || c == '^' || c == '$' || c == '*' || c == '?' || 
                                c == '[' || c == ']' || c == '{' || c == '}' || c == '(' || 
                                c == ')' || c == '|' || c == '\\')
                        {
                            // Escape regex special characters
                            escapedPattern += '\\';
                            escapedPattern += c;
                        }
                        else
                        {
                            escapedPattern += c;
                        }
                    }
                    
                    LOGM_DEBUG(TAG, "AWS topic pattern: %s -> regex: %s", awsTopic.c_str(), escapedPattern.c_str());
                    
                    return std::regex(escapedPattern);
                }

                bool RouteMatcher::matchesExact(const std::string& topic, const std::string& pattern) const
                {
                    // For up routes, support simple + wildcards in local topics
                    if (pattern.find('#') != std::string::npos)
                    {
                        // Multi-level wildcards not supported in local topics for up routes
                        return false;
                    }
                    
                    if (pattern.find('+') == std::string::npos)
                    {
                        // No wildcards, exact match
                        return topic == pattern;
                    }
                    
                    // Split into segments and match with + wildcards
                    auto splitTopic = [](const std::string& str) -> std::vector<std::string> {
                        std::vector<std::string> segments;
                        std::stringstream ss(str);
                        std::string segment;
                        while (std::getline(ss, segment, '/'))
                        {
                            segments.push_back(segment);
                        }
                        return segments;
                    };
                    
                    std::vector<std::string> topicSegments = splitTopic(topic);
                    std::vector<std::string> patternSegments = splitTopic(pattern);
                    
                    if (topicSegments.size() != patternSegments.size())
                    {
                        return false;
                    }
                    
                    for (size_t i = 0; i < topicSegments.size(); ++i)
                    {
                        if (patternSegments[i] != "+" && patternSegments[i] != topicSegments[i])
                        {
                            return false;
                        }
                    }
                    
                    return true;
                }

                std::regex RouteMatcher::compileLocalTopicPattern(const std::string& localTopic, std::vector<std::string>& captureNames) const
                {
                    captureNames.clear();
                    std::string pattern;
                    int matchCount = 0;
                    for(char c : localTopic)
                    {
                        if (c == '+')
                        {
                            matchCount++;
                            captureNames.push_back("match" + std::to_string(matchCount));
                            pattern += "([^/]+)";
                        }
                        else if (c == '.' || c == '^' || c == '$' || c == '*' || c == '?' || 
                                 c == '[' || c == ']' || c == '{' || c == '}' || c == '(' || 
                                 c == ')' || c == '|' || c == '\\')
                        {
                            pattern += '\\';
                            pattern += c;
                        }
                        else
                        {
                            pattern += c;
                        }
                    }
                    return std::regex(pattern);
                }

            } // namespace LocalMqttBridge
        } // namespace DeviceClient
    } // namespace Iot
} // namespace Aws