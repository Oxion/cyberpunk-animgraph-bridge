#include "Bridge/CustomParam/LookAt.hpp"

#include "Bridge/CustomParam/Common.hpp"

#include <RED4ext/Scripting/Natives/Generated/anim/LookAtRef.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/LookAtRequest.hpp>
#include <RED4ext/Scripting/Natives/WorldPosition.hpp>

#include <cstring>

namespace Bridge::CustomParam
{
namespace
{
nlohmann::json LookAtRefToJson(const RED4ext::anim::LookAtRef& lookAtRef)
{
    return {{"id", lookAtRef.id}, {"part", SafeCName(lookAtRef.part)}};
}

nlohmann::json LookAtLimitsToJson(const RED4ext::anim::LookAtLimits& limits)
{
    return {
        {"softLimitDegrees", limits.softLimitDegrees},
        {"hardLimitDegrees", limits.hardLimitDegrees},
        {"hardLimitDistance", limits.hardLimitDistance},
        {"backLimitDegrees", limits.backLimitDegrees},
    };
}

nlohmann::json LookAtRequestToJson(const RED4ext::anim::LookAtRequest& request)
{
    nlohmann::json additionalPartsJson = nlohmann::json::array();
    const uint32_t additionalPartCount = request.additionalParts.Size();
    for (uint32_t index = 0; index < additionalPartCount && index < 2; ++index)
    {
        const auto& additionalPart = request.additionalParts[index];
        additionalPartsJson.push_back({
            {"partName", SafeCName(additionalPart.partName)},
            {"weight", additionalPart.weight},
            {"suppress", additionalPart.suppress},
            {"mode", additionalPart.mode},
        });
    }
    return {
        {"transitionSpeed", request.transitionSpeed},
        {"hasOutTransition", request.hasOutTransition},
        {"outTransitionSpeed", request.outTransitionSpeed},
        {"followingSpeedFactorOverride", request.followingSpeedFactorOverride},
        {"limits", LookAtLimitsToJson(request.limits)},
        {"suppress", request.suppress},
        {"mode", request.mode},
        {"priority", request.priority},
        {"calculatePositionInParentSpace", request.calculatePositionInParentSpace},
        {"additionalParts", std::move(additionalPartsJson)},
    };
}

struct UpdatePositionsInfoLayout
{
    RED4ext::anim::LookAtRef lookAtRef;
    RED4ext::WorldPosition externalTargetPositionWs;
    uint8_t pad[0x20 - 0x10 - 0xC];
};
static_assert(sizeof(UpdatePositionsInfoLayout) == 0x20);
} // namespace

std::optional<nlohmann::json> TrySerializeLookAtAdd(const char* instanceBytes)
{
    if (!instanceBytes)
        return std::nullopt;

    RED4ext::anim::LookAtRef lookAtRef{};
    RED4ext::anim::LookAtRequest request{};
    std::memcpy(&lookAtRef, instanceBytes + 0x30, sizeof(lookAtRef));
    std::memcpy(&request, instanceBytes + 0x40, sizeof(request));
    return nlohmann::json{
        {"kind", "LookAtParams_Add"},
        {"ref", LookAtRefToJson(lookAtRef)},
        {"request", LookAtRequestToJson(request)},
    };
}

std::optional<nlohmann::json> TrySerializeLookAtRemove(const char* instanceBytes)
{
    if (!instanceBytes)
        return std::nullopt;

    RED4ext::anim::LookAtRef lookAtRef{};
    bool hasOutTransition = false;
    float outTransitionSpeed = 0.f;
    std::memcpy(&lookAtRef, instanceBytes + 0x30, sizeof(lookAtRef));
    std::memcpy(&hasOutTransition, instanceBytes + 0x40, sizeof(hasOutTransition));
    std::memcpy(&outTransitionSpeed, instanceBytes + 0x44, sizeof(outTransitionSpeed));
    return nlohmann::json{
        {"kind", "LookAtParams_Remove"},
        {"ref", LookAtRefToJson(lookAtRef)},
        {"hasOutTransition", hasOutTransition},
        {"outTransitionSpeed", outTransitionSpeed},
    };
}

std::optional<nlohmann::json> TrySerializeLookAtUpdatePositions(const char* instanceBytes)
{
    if (!instanceBytes)
        return std::nullopt;

    DynArrayView positionsArray{};
    std::memcpy(&positionsArray, instanceBytes + 0x30, sizeof(positionsArray));
    nlohmann::json positionInfos = nlohmann::json::array();
    if (positionsArray.entries && positionsArray.size > 0 && positionsArray.size <= 64)
    {
        const auto* infoEntries = static_cast<const UpdatePositionsInfoLayout*>(positionsArray.entries);
        for (uint32_t index = 0; index < positionsArray.size; ++index)
        {
            const auto targetVector = infoEntries[index].externalTargetPositionWs.AsVector4();
            positionInfos.push_back({
                {"ref", LookAtRefToJson(infoEntries[index].lookAtRef)},
                {"targetWs",
                 {{"x", targetVector.X}, {"y", targetVector.Y}, {"z", targetVector.Z}, {"w", targetVector.W}}},
            });
        }
    }
    return nlohmann::json{{"kind", "LookAtParams_UpdatePositions"}, {"infos", std::move(positionInfos)}};
}
}
