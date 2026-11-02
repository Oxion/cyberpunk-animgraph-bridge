#include "Bridge/CustomParam/Serialize.hpp"

#include "Bridge/CustomParam/Common.hpp"
#include "Bridge/CustomParam/LookAt.hpp"
#include "Bridge/CustomParam/MixerSlot.hpp"
#include "Bridge/Seh.h"

namespace Bridge
{
namespace
{
struct SerCtx
{
    RED4ext::ISerializable* inst{};
    nlohmann::json event;
    bool ok{};
};

void SerThunk(void* userData)
{
    auto* serializeContext = static_cast<SerCtx*>(userData);
    if (!serializeContext->inst)
        return;

    auto* instanceClass = serializeContext->inst->GetType();
    if (!instanceClass)
        return;

    const std::string kind =
        CustomParam::KindFromTypeName(CustomParam::SafeCName(instanceClass->name));
    const char* instanceBytes = reinterpret_cast<const char*>(serializeContext->inst);

    std::optional<nlohmann::json> serializedEvent;
    if (kind == "MixerSlotParams")
        serializedEvent = CustomParam::TrySerializeMixerSlotParams(instanceBytes);
    else if (kind == "LookAtParams_Add")
        serializedEvent = CustomParam::TrySerializeLookAtAdd(instanceBytes);
    else if (kind == "LookAtParams_Remove")
        serializedEvent = CustomParam::TrySerializeLookAtRemove(instanceBytes);
    else if (kind == "LookAtParams_UpdatePositions")
        serializedEvent = CustomParam::TrySerializeLookAtUpdatePositions(instanceBytes);

    if (!serializedEvent)
        return;

    serializeContext->event = std::move(*serializedEvent);
    serializeContext->ok = true;
}
} // namespace

std::optional<nlohmann::json> SerializeCustomParam(RED4ext::ISerializable* data)
{
    if (!data)
        return std::nullopt;

    SerCtx serializeContext{data, {}, false};
    if (!Bridge_SehRun(&SerThunk, &serializeContext))
        return std::nullopt;
    if (!serializeContext.ok)
        return std::nullopt;
    return serializeContext.event;
}
}
