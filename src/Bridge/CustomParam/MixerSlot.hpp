#pragma once

#include <optional>

#include <nlohmann/json.hpp>

namespace Bridge::CustomParam
{
/** Serialize animMixerSlotParams payload (base = ISerializable*). */
std::optional<nlohmann::json> TrySerializeMixerSlotParams(const char* instanceBytes);
}
