#pragma once

#include <optional>

#include <RED4ext/RED4ext.hpp>
#include <nlohmann/json.hpp>

namespace Bridge
{
/** Serialize PushCustomParam payload → SimServiceEvent-shaped JSON, or nullopt if unsupported. */
std::optional<nlohmann::json> SerializeCustomParam(RED4ext::ISerializable* data);
}
