#include "Bridge/FeatureSerialize.hpp"

#include "Bridge/CNameUtil.hpp"
#include "Bridge/Seh.h"

#include <RED4ext/RTTITypes.hpp>
#include <RED4ext/Scripting/Natives/Quaternion.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>
#include <RED4ext/rtti/ERTTIType.hpp>
#include <cstdio>
#include <cstring>

namespace Bridge
{
namespace
{
struct TypeCache
{
    RED4ext::rtti::IType* Float{};
    RED4ext::rtti::IType* Double{};
    RED4ext::rtti::IType* Bool{};
    RED4ext::rtti::IType* Int32{};
    RED4ext::rtti::IType* Uint32{};
    RED4ext::rtti::IType* Int16{};
    RED4ext::rtti::IType* Uint16{};
    RED4ext::rtti::IType* Int8{};
    RED4ext::rtti::IType* Uint8{};
    RED4ext::rtti::IType* Int64{};
    RED4ext::rtti::IType* Uint64{};
    RED4ext::rtti::IType* CName{};
    RED4ext::rtti::IType* Vector4{};
    RED4ext::rtti::IType* Quaternion{};
    bool ready{};
};

TypeCache& Types()
{
    static TypeCache cache;
    if (!cache.ready)
    {
        auto* rtti = RED4ext::CRTTISystem::Get();
        if (!rtti)
            return cache;
        cache.Float = rtti->GetType("Float");
        cache.Double = rtti->GetType("Double");
        cache.Bool = rtti->GetType("Bool");
        cache.Int32 = rtti->GetType("Int32");
        cache.Uint32 = rtti->GetType("Uint32");
        cache.Int16 = rtti->GetType("Int16");
        cache.Uint16 = rtti->GetType("Uint16");
        cache.Int8 = rtti->GetType("Int8");
        cache.Uint8 = rtti->GetType("Uint8");
        cache.Int64 = rtti->GetType("Int64");
        cache.Uint64 = rtti->GetType("Uint64");
        cache.CName = rtti->GetType("CName");
        cache.Vector4 = rtti->GetType("Vector4");
        cache.Quaternion = rtti->GetType("Quaternion");
        cache.ready = cache.Float != nullptr;
    }
    return cache;
}

constexpr uint32_t kMinDataOffset = 0x40;
constexpr int kMaxProps = 64;

struct PropOut
{
    char name[64]{};
    char type[8]{};
    char value[160]{};
};

struct WalkCtx
{
    RED4ext::IScriptable* feature{};
    PropOut out[kMaxProps]{};
    int count{};
};

void WalkThunk(void* userData)
{
    auto* walkContext = static_cast<WalkCtx*>(userData);
    auto* feature = walkContext->feature;
    auto* featureClass = feature->GetType();
    if (!featureClass || featureClass->size < kMinDataOffset)
        return;

    const auto& typeCache = Types();
    const char* featureBytes = reinterpret_cast<const char*>(feature);

    // Prefer GetProperties over raw unk118 — former is the stable RTTI entry.
    RED4ext::DynArray<RED4ext::CProperty*> properties;
    featureClass->GetProperties(properties);
    const uint32_t propertyCount = properties.size();

    for (uint32_t index = 0; index < propertyCount && walkContext->count < kMaxProps; ++index)
    {
        auto* property = properties[index];
        if (!property || !property->type)
            continue;

        const uint32_t valueOffset = property->valueOffset;
        if (valueOffset < kMinDataOffset || valueOffset >= featureClass->size)
            continue;

        const char* propertyName = SafeCName(property->name);
        if (!propertyName || !*propertyName)
            continue;

        auto* propertyType = property->type;
        PropOut& propOut = walkContext->out[walkContext->count];
        std::snprintf(propOut.name, sizeof(propOut.name), "%s", propertyName);

        if (propertyType == typeCache.Float || propertyType == typeCache.Double)
        {
            if (valueOffset + sizeof(float) > featureClass->size)
                continue;
            float floatValue = 0.f;
            std::memcpy(&floatValue, featureBytes + valueOffset, sizeof(floatValue));
            std::snprintf(propOut.value, sizeof(propOut.value), "%.9g", static_cast<double>(floatValue));
            std::snprintf(propOut.type, sizeof(propOut.type), "float");
            ++walkContext->count;
        }
        else if (propertyType == typeCache.Bool)
        {
            if (valueOffset + sizeof(bool) > featureClass->size)
                continue;
            bool boolValue = false;
            std::memcpy(&boolValue, featureBytes + valueOffset, sizeof(boolValue));
            std::snprintf(propOut.value, sizeof(propOut.value), "%s", boolValue ? "true" : "false");
            std::snprintf(propOut.type, sizeof(propOut.type), "bool");
            ++walkContext->count;
        }
        else if (propertyType == typeCache.Int32 || propertyType == typeCache.Uint32 ||
                 propertyType == typeCache.Int16 || propertyType == typeCache.Uint16 ||
                 propertyType == typeCache.Int8 || propertyType == typeCache.Uint8 ||
                 propertyType == typeCache.Int64 || propertyType == typeCache.Uint64)
        {
            if (valueOffset + sizeof(int32_t) > featureClass->size)
                continue;
            int32_t intValue = 0;
            std::memcpy(&intValue, featureBytes + valueOffset, sizeof(intValue));
            std::snprintf(propOut.value, sizeof(propOut.value), "%d", intValue);
            std::snprintf(propOut.type, sizeof(propOut.type), "int");
            ++walkContext->count;
        }
        else if (propertyType->GetType() == RED4ext::rtti::ERTTIType::Enum ||
                 propertyType->GetType() == RED4ext::rtti::ERTTIType::BitField)
        {
            // Scene.tier (GameplayTier) etc. — IntFeature reads these as Int32.
            uint8_t storageSize = 4;
            if (propertyType->GetType() == RED4ext::rtti::ERTTIType::Enum)
                storageSize = static_cast<RED4ext::CEnum*>(propertyType)->actualSize;
            else
                storageSize = static_cast<RED4ext::CBitfield*>(propertyType)->actualSize;
            if (storageSize != 1 && storageSize != 2 && storageSize != 4 && storageSize != 8)
                storageSize = 4;
            if (valueOffset + storageSize > featureClass->size)
                continue;
            int64_t enumValue = 0;
            if (storageSize == 1)
            {
                int8_t byteValue = 0;
                std::memcpy(&byteValue, featureBytes + valueOffset, 1);
                enumValue = byteValue;
            }
            else if (storageSize == 2)
            {
                int16_t wordValue = 0;
                std::memcpy(&wordValue, featureBytes + valueOffset, 2);
                enumValue = wordValue;
            }
            else if (storageSize == 4)
            {
                int32_t dwordValue = 0;
                std::memcpy(&dwordValue, featureBytes + valueOffset, 4);
                enumValue = dwordValue;
            }
            else
                std::memcpy(&enumValue, featureBytes + valueOffset, 8);
            std::snprintf(propOut.value, sizeof(propOut.value), "%d", static_cast<int>(enumValue));
            std::snprintf(propOut.type, sizeof(propOut.type), "int");
            ++walkContext->count;
        }
        else if (propertyType == typeCache.CName)
        {
            if (valueOffset + sizeof(RED4ext::CName) > featureClass->size)
                continue;
            RED4ext::CName cnameValue{};
            std::memcpy(&cnameValue, featureBytes + valueOffset, sizeof(cnameValue));
            const char* cnameText = SafeCName(cnameValue);
            std::snprintf(propOut.value, sizeof(propOut.value), "\"%s\"", cnameText ? cnameText : "");
            std::snprintf(propOut.type, sizeof(propOut.type), "cname");
            ++walkContext->count;
        }
        else if (propertyType == typeCache.Vector4)
        {
            if (valueOffset + sizeof(RED4ext::Vector4) > featureClass->size)
                continue;
            RED4ext::Vector4 vectorValue{};
            std::memcpy(&vectorValue, featureBytes + valueOffset, sizeof(vectorValue));
            std::snprintf(propOut.value, sizeof(propOut.value), "{\"x\":%.9g,\"y\":%.9g,\"z\":%.9g,\"w\":%.9g}",
                          static_cast<double>(vectorValue.X), static_cast<double>(vectorValue.Y),
                          static_cast<double>(vectorValue.Z), static_cast<double>(vectorValue.W));
            std::snprintf(propOut.type, sizeof(propOut.type), "vector");
            ++walkContext->count;
        }
        else if (propertyType == typeCache.Quaternion)
        {
            if (valueOffset + sizeof(RED4ext::Quaternion) > featureClass->size)
                continue;
            RED4ext::Quaternion quaternionValue{};
            std::memcpy(&quaternionValue, featureBytes + valueOffset, sizeof(quaternionValue));
            std::snprintf(propOut.value, sizeof(propOut.value), "{\"x\":%.9g,\"y\":%.9g,\"z\":%.9g,\"w\":%.9g}",
                          static_cast<double>(quaternionValue.i), static_cast<double>(quaternionValue.j),
                          static_cast<double>(quaternionValue.k), static_cast<double>(quaternionValue.r));
            std::snprintf(propOut.type, sizeof(propOut.type), "quat");
            ++walkContext->count;
        }
    }
}
} // namespace

void ForEachAnimFeatureProperty(RED4ext::IScriptable* feature, const FeaturePropFn& callback)
{
    if (!feature || !callback)
        return;
    if (!Types().ready)
        return;
    if (!Bridge_IsReadable(feature, kMinDataOffset))
        return;

    WalkCtx walkContext{};
    walkContext.feature = feature;
    if (!Bridge_SehRun(&WalkThunk, &walkContext))
        return;

    for (int index = 0; index < walkContext.count; ++index)
        callback(walkContext.out[index].name, walkContext.out[index].type, walkContext.out[index].value);
}
}
