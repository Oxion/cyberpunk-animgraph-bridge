#pragma once

#include <optional>

#include <nlohmann/json.hpp>

namespace Bridge::CustomParam
{
std::optional<nlohmann::json> TrySerializeLookAtAdd(const char* instanceBytes);
std::optional<nlohmann::json> TrySerializeLookAtRemove(const char* instanceBytes);
std::optional<nlohmann::json> TrySerializeLookAtUpdatePositions(const char* instanceBytes);
}
