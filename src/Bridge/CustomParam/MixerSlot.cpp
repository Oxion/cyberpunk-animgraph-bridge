#include "Bridge/CustomParam/MixerSlot.hpp"

#include "Bridge/CustomParam/Common.hpp"

#include <RED4ext/CName.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/MixerSlotParams.hpp>

#include <cstring>

namespace Bridge::CustomParam
{
namespace
{
const char* MuteToString(uint32_t mute)
{
    if (mute == 0)
        return "NONE";
    if (mute == 3)
        return "ALL";
    if (mute & 2u)
        return (mute & 1u) ? "ALL" : "FACE_ANIMS";
    if (mute & 1u)
        return "STANDARD";
    return "NONE";
}

// MSVC layout from anim::MixerSlotAnimParams (no RTTI props).
#pragma pack(push, 8)
struct MixerSlotAnimParamsLayout
{
    RED4ext::CName animationName;
    RED4ext::CName fallbackAnimationName;
    float previousTime;
    float currentTime;
    float regularTime;
    int32_t loops;
    float weight;
    float facialNeckWeight;
    bool upperFaceBlendAdditive;
    bool lowerFaceBlendAdditive;
    bool eyesBlendAdditive;
    uint8_t pad2B[5];
    RED4ext::CName bodyPartMask;
    bool isFacial;
    bool extractTrajectoryForBlend;
    uint8_t pad3A[6];
    uint64_t voStringId;
    bool e3_hack_isRid;
    bool convertToAdditive;
    uint8_t pad4A[2];
    uint32_t muteAnimEvents;
    bool useDynamicAnimSets;
    uint8_t pad51[7];
};
#pragma pack(pop)
static_assert(sizeof(MixerSlotAnimParamsLayout) == 0x58);

#pragma pack(push, 8)
struct MixerSlotParamsData
{
    int32_t dest;
    bool enableInertiaBlend;
    uint8_t pad35[3];
    DynArrayView normal;
    DynArrayView additive;
    DynArrayView overrideList;
    bool isFacial;
    uint8_t pad69[7];
};
#pragma pack(pop)
static_assert(sizeof(MixerSlotParamsData) == 0x40);
static_assert(sizeof(RED4ext::anim::MixerSlotParams) == 0x70);

nlohmann::json AnimParamsToJson(const MixerSlotAnimParamsLayout& animParams)
{
    return {
        {"animationName", SafeCName(animParams.animationName)},
        {"previousTime", animParams.previousTime},
        {"currentTime", animParams.currentTime},
        {"loops", animParams.loops},
        {"weight", animParams.weight},
        {"bodyPartMask", SafeCName(animParams.bodyPartMask)},
        {"convertToAdditive", animParams.convertToAdditive},
        {"extractTrajectoryForBlend", animParams.extractTrajectoryForBlend},
        {"useDynamicAnimSets", animParams.useDynamicAnimSets},
        {"muteAnimEvents", MuteToString(animParams.muteAnimEvents)},
    };
}

nlohmann::json DynArrayAnimParamsToJson(const DynArrayView& animParamsArray)
{
    nlohmann::json animParamsJson = nlohmann::json::array();
    if (!animParamsArray.entries || animParamsArray.size == 0 || animParamsArray.size > 64)
        return animParamsJson;

    const auto* layoutEntries = static_cast<const MixerSlotAnimParamsLayout*>(animParamsArray.entries);
    for (uint32_t index = 0; index < animParamsArray.size; ++index)
        animParamsJson.push_back(AnimParamsToJson(layoutEntries[index]));
    return animParamsJson;
}
} // namespace

std::optional<nlohmann::json> TrySerializeMixerSlotParams(const char* instanceBytes)
{
    if (!instanceBytes)
        return std::nullopt;

    MixerSlotParamsData mixerData{};
    std::memcpy(&mixerData, instanceBytes + 0x30, sizeof(mixerData));
    return nlohmann::json{
        {"kind", "MixerSlotParams"},
        {"isFacial", mixerData.isFacial},
        {"enableInertiaBlend", mixerData.enableInertiaBlend},
        {"normalAnimParamsList", DynArrayAnimParamsToJson(mixerData.normal)},
        {"additiveAnimParamsList", DynArrayAnimParamsToJson(mixerData.additive)},
        {"overrideAnimParamsList", DynArrayAnimParamsToJson(mixerData.overrideList)},
    };
}
}
