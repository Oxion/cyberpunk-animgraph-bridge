#pragma once

#include "Bridge/JsonEscape.hpp"

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace Bridge::Op
{
inline constexpr std::string_view kSnapshot = "snapshot";
inline constexpr std::string_view kSetVar = "setVar";
inline constexpr std::string_view kSetFeature = "setFeature";
inline constexpr std::string_view kSetWrapper = "setWrapper";
inline constexpr std::string_view kTick = "tick";
inline constexpr std::string_view kServiceEvent = "serviceEvent";
}

namespace Bridge
{
inline std::string MakeSetVar(const std::string& name, std::string_view type, const std::string& valueJson)
{
    std::ostringstream out;
    out << "{\"op\":\"" << Op::kSetVar << "\",\"name\":\"" << JsonEscape(name) << "\",\"type\":\"" << type
        << "\",\"value\":" << valueJson << '}';
    return out.str();
}

inline std::string MakeSetFeature(const std::string& feature, const std::string& propsJsonArray)
{
    std::ostringstream out;
    out << "{\"op\":\"" << Op::kSetFeature << "\",\"feature\":\"" << JsonEscape(feature) << "\",\"props\":"
        << propsJsonArray << '}';
    return out.str();
}

inline std::string MakeSetWrapper(const std::string& name, const std::string& valueJson)
{
    std::ostringstream out;
    out << "{\"op\":\"" << Op::kSetWrapper << "\",\"name\":\"" << JsonEscape(name) << "\",\"value\":" << valueJson
        << '}';
    return out.str();
}

inline std::string MakeTick(double dt, double rawDt, const std::vector<std::string>& externalEvents)
{
    std::ostringstream out;
    out << "{\"op\":\"" << Op::kTick << "\",\"dt\":" << dt << ",\"rawDt\":" << rawDt << ",\"externalEvents\":[";
    for (size_t eventIndex = 0; eventIndex < externalEvents.size(); ++eventIndex)
    {
        if (eventIndex)
            out << ',';
        out << '"' << JsonEscape(externalEvents[eventIndex]) << '"';
    }
    out << "]}";
    return out.str();
}

inline std::string MakeServiceEvent(const std::string& eventJsonObject)
{
    std::ostringstream out;
    out << "{\"op\":\"" << Op::kServiceEvent << "\",\"event\":" << eventJsonObject << '}';
    return out.str();
}
}
