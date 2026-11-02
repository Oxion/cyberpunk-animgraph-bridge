#include "Bridge/InstanceBufferDump.hpp"

#include "Bridge/Addresses.hpp"
#include "Bridge/CNameUtil.hpp"
#include "Bridge/FeatureSerialize.hpp"
#include "Bridge/JsonEscape.hpp"
#include "Bridge/Seh.h"

#include <RED4ext/Containers/StaticArray.hpp>
#include <RED4ext/Handle.hpp>
#include <RED4ext/Relocation.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimGraph.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimVariable.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimVariableContainer.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/AnimationControllerComponent.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Bridge
{
namespace
{
bool IsLikelyHeapPtr(const void* pointer)
{
    const auto address = reinterpret_cast<uintptr_t>(pointer);
    return address >= 0x10000ull && address < 0x00007FFFFFFFFFFFull;
}

enum class EAnimInputType : int32_t
{
    InputUnknown = 0,
    InputFloat,
    InputDouble,
    InputBool,
    InputInt,
    InputName,
    InputVector,
    InputQuaternion,
    InputTransform,
};

struct RuntimeInputInfo
{
    uint32_t offset;
    uint32_t _pad0;
    RED4ext::CName groupName;
    RED4ext::CName inputName;
    EAnimInputType type;
    uint32_t _pad1;
};
static_assert(sizeof(RuntimeInputInfo) == 32, "RuntimeInputInfo packing");

using FindInputEntry_t = const RuntimeInputInfo* (*)(void* layout, RED4ext::CName group, RED4ext::CName name,
                                                     bool isBackground);

FindInputEntry_t g_findInputEntry{};
std::atomic<bool> g_fnsReady{false};

std::mutex g_cacheMutex;
void* g_graph{};
void* g_instance{};
void* g_layout{};
int32_t g_layoutPtrOff{-1};

struct SehPtr
{
    void* addr{};
    void* value{};
};
void ReadPtrThunk(void* userData)
{
    auto* context = static_cast<SehPtr*>(userData);
    context->value = *reinterpret_cast<void**>(context->addr);
}

struct SehFind
{
    void* layout{};
    RED4ext::CName group{};
    RED4ext::CName prop{};
    bool background{};
    const RuntimeInputInfo* info{};
};
void FindThunk(void* userData)
{
    auto* context = static_cast<SehFind*>(userData);
    context->info = g_findInputEntry(context->layout, context->group, context->prop, context->background);
}

struct SehBuf
{
    void* instance{};
    void* data{};
    uint32_t size{};
};
void BufThunk(void* userData)
{
    auto* context = static_cast<SehBuf*>(userData);
    // Inlined GetVarsBufferData/Size — members at +0 / +8 (no vtable).
    auto* instanceBase = static_cast<char*>(context->instance);
    void* bufferData = *reinterpret_cast<void**>(instanceBase);
    uint32_t bufferSize = *reinterpret_cast<uint32_t*>(instanceBase + 8);
    if (!IsLikelyHeapPtr(bufferData) || bufferSize == 0 || bufferSize > 0x1000000u)
    {
        // Fallback if a vptr were ever present.
        bufferData = *reinterpret_cast<void**>(instanceBase + 8);
        bufferSize = *reinterpret_cast<uint32_t*>(instanceBase + 16);
    }
    if (!IsLikelyHeapPtr(bufferData) || bufferSize == 0 || bufferSize > 0x1000000u)
        return;
    context->data = bufferData;
    context->size = bufferSize;
}

struct SehDyn
{
    void* layout{};
    int32_t off{};
    RuntimeInputInfo* entries{};
    uint32_t size{};
};
void DynThunk(void* userData)
{
    auto* context = static_cast<SehDyn*>(userData);
    auto* layoutBase = static_cast<char*>(context->layout) + context->off;
    context->entries = *reinterpret_cast<RuntimeInputInfo**>(layoutBase);
    context->size = *reinterpret_cast<uint32_t*>(layoutBase + 8);
}

struct SehCopy
{
    void* dst{};
    const void* src{};
    size_t byteCount{};
};
void CopyThunk(void* userData)
{
    auto* context = static_cast<SehCopy*>(userData);
    std::memcpy(context->dst, context->src, context->byteCount);
}

struct SehType
{
    RuntimeInputInfo* entries{};
    EAnimInputType type{};
};
void TypeThunk(void* userData)
{
    auto* context = static_cast<SehType*>(userData);
    context->type = context->entries[0].type;
}

bool EnsureFns()
{
    if (g_fnsReady.load(std::memory_order_acquire))
        return g_findInputEntry != nullptr;

    g_findInputEntry = reinterpret_cast<FindInputEntry_t>(
        RED4ext::UniversalRelocBase::Resolve(AddressLib::AnimInstanceBufferLayout_FindInputEntry));
    g_fnsReady.store(true, std::memory_order_release);
    return g_findInputEntry != nullptr;
}

bool TryReadInfo(void* layout, RED4ext::CName group, RED4ext::CName prop, const RuntimeInputInfo** outInfo)
{
    if (!layout || !g_findInputEntry || !outInfo)
        return false;
    SehFind findContext{layout, group, prop, false, nullptr};
    if (!Bridge_SehRun(&FindThunk, &findContext) || !findContext.info)
    {
        findContext.background = true;
        findContext.info = nullptr;
        if (!Bridge_SehRun(&FindThunk, &findContext) || !findContext.info)
            return false;
    }
    *outInfo = findContext.info;
    return true;
}

void* ProbeLayoutPtr(void* graph, RED4ext::CName group, RED4ext::CName prop)
{
    if (!graph || !EnsureFns())
        return nullptr;

    for (int32_t byteOffset = 8; byteOffset <= 0x280; byteOffset += 8)
    {
        SehPtr readPtr{static_cast<char*>(graph) + byteOffset, nullptr};
        if (!Bridge_SehRun(&ReadPtrThunk, &readPtr) || !readPtr.value)
            continue;
        if (reinterpret_cast<uintptr_t>(readPtr.value) < 0x10000)
            continue;

        const RuntimeInputInfo* inputInfo = nullptr;
        if (!TryReadInfo(readPtr.value, group, prop, &inputInfo))
            continue;

        g_layoutPtrOff = byteOffset;
        return readPtr.value;
    }
    return nullptr;
}

void* ResolveLayout(void* graph, RED4ext::CName group, RED4ext::CName prop)
{
    if (!graph)
        return nullptr;
    if (g_layoutPtrOff >= 0)
    {
        SehPtr readPtr{static_cast<char*>(graph) + g_layoutPtrOff, nullptr};
        if (Bridge_SehRun(&ReadPtrThunk, &readPtr) && readPtr.value)
            return readPtr.value;
    }
    return ProbeLayoutPtr(graph, group, prop);
}

bool ReadSlotJson(void* instance, const RuntimeInputInfo& info, std::string& typeOut, std::string& valueOut)
{
    if (!EnsureFns() || !instance)
        return false;

    SehBuf bufferContext{instance, nullptr, 0};
    if (!Bridge_SehRun(&BufThunk, &bufferContext) || !bufferContext.data ||
        !IsLikelyHeapPtr(bufferContext.data))
        return false;

    auto hasBytesAtOffset = [&](uint32_t byteCount) {
        return info.offset < bufferContext.size && info.offset + byteCount <= bufferContext.size;
    };
    const char* slotBytes = static_cast<const char*>(bufferContext.data) + info.offset;

    switch (info.type)
    {
    case EAnimInputType::InputBool:
    {
        if (!hasBytesAtOffset(1) || !Bridge_IsReadable(slotBytes, 1))
            return false;
        const bool boolValue = *reinterpret_cast<const uint8_t*>(slotBytes) != 0;
        typeOut = "bool";
        valueOut = boolValue ? "true" : "false";
        return true;
    }
    case EAnimInputType::InputFloat:
    {
        float floatScratch[4]{};
        if (!hasBytesAtOffset(4) || !Bridge_TryReadF4(reinterpret_cast<const float*>(slotBytes), floatScratch))
            return false;
        typeOut = "float";
        char formatBuffer[64];
        std::snprintf(formatBuffer, sizeof(formatBuffer), "%.9g", static_cast<double>(floatScratch[0]));
        valueOut = formatBuffer;
        return true;
    }
    case EAnimInputType::InputDouble:
    {
        if (!hasBytesAtOffset(8) || !Bridge_IsReadable(slotBytes, 8))
            return false;
        double doubleValue = 0.0;
        std::memcpy(&doubleValue, slotBytes, 8);
        typeOut = "float";
        char formatBuffer[64];
        std::snprintf(formatBuffer, sizeof(formatBuffer), "%.9g", doubleValue);
        valueOut = formatBuffer;
        return true;
    }
    case EAnimInputType::InputInt:
    {
        if (!hasBytesAtOffset(4) || !Bridge_IsReadable(slotBytes, 4))
            return false;
        int32_t intValue = 0;
        std::memcpy(&intValue, slotBytes, 4);
        typeOut = "int";
        valueOut = std::to_string(intValue);
        return true;
    }
    case EAnimInputType::InputName:
    {
        if (!hasBytesAtOffset(8) || !Bridge_IsReadable(slotBytes, 8))
            return false;
        RED4ext::CName cnameValue{};
        std::memcpy(&cnameValue, slotBytes, sizeof(cnameValue));
        typeOut = "cname";
        valueOut = std::string("\"") + JsonEscape(SafeCName(cnameValue)) + "\"";
        return true;
    }
    case EAnimInputType::InputVector:
    case EAnimInputType::InputQuaternion:
    {
        float components[4]{};
        if (!hasBytesAtOffset(16) || !Bridge_TryReadF4(reinterpret_cast<const float*>(slotBytes), components))
            return false;
        typeOut = (info.type == EAnimInputType::InputVector) ? "vector" : "quat";
        char formatBuffer[128];
        std::snprintf(formatBuffer, sizeof(formatBuffer), "{\"x\":%.9g,\"y\":%.9g,\"z\":%.9g,\"w\":%.9g}",
                      components[0], components[1], components[2], components[3]);
        valueOut = formatBuffer;
        return true;
    }
    default:
        return false;
    }
}

bool CollectInputsArray(void* layout, const RuntimeInputInfo* sample, std::vector<RuntimeInputInfo>& out)
{
    if (!layout || !sample)
        return false;

    for (int32_t byteOffset = 0; byteOffset <= 0x100; byteOffset += 8)
    {
        SehDyn dynContext{layout, byteOffset, nullptr, 0};
        if (!Bridge_SehRun(&DynThunk, &dynContext) || !dynContext.entries || dynContext.size == 0 ||
            dynContext.size > 4096)
            continue;
        if (sample < dynContext.entries || sample >= dynContext.entries + dynContext.size)
            continue;

        out.resize(dynContext.size);
        SehCopy copyContext{out.data(), dynContext.entries, sizeof(RuntimeInputInfo) * dynContext.size};
        if (!Bridge_SehRun(&CopyThunk, &copyContext))
        {
            out.clear();
            return false;
        }
        return true;
    }
    return false;
}

RED4ext::CName FirstFeaturePropName(RED4ext::IScriptable* feature)
{
    RED4ext::CName found{};
    if (!feature)
        return found;
    ForEachAnimFeatureProperty(feature, [&](const std::string& property, const std::string&, const std::string&) {
        if (found.hash || property.empty())
            return;
        found = RED4ext::CName(property.c_str());
    });
    return found;
}
} // namespace

void InstanceDump_OnAnimGraphApplyFeature(void* graph, void* instance, RED4ext::CName group,
                                          const RED4ext::IScriptable* feature, bool applied,
                                          bool streamCandidate)
{
    (void)group;
    (void)feature;
    // Hot path: only stash pointers. Layout / RTTI walks happen on snapshot under SEH.
    if (!applied || !graph || !instance || !streamCandidate)
        return;
    const auto graphAddress = reinterpret_cast<uintptr_t>(graph);
    const auto instanceAddress = reinterpret_cast<uintptr_t>(instance);
    if (graphAddress < 0x10000ull || instanceAddress < 0x10000ull)
        return;

    std::lock_guard lock(g_cacheMutex);
    g_graph = graph;
    g_instance = instance;
}

void InstanceDump_OnTempSetVariable(void* graph, void* instance)
{
    if (!graph || !instance)
        return;
    std::lock_guard lock(g_cacheMutex);
    if (g_graph == graph || g_instance == instance)
    {
        g_graph = graph;
        g_instance = instance;
    }
}

void InstanceDump_Clear()
{
    std::lock_guard lock(g_cacheMutex);
    g_graph = nullptr;
    g_instance = nullptr;
    g_layout = nullptr;
    g_layoutPtrOff = -1;
}

bool InstanceDump_TryBuildFeaturesJson(std::string& outFeaturesObject, const char** outErr)
{
    if (outErr)
        *outErr = nullptr;
    if (!EnsureFns())
    {
        if (outErr)
            *outErr = "resolve-fns";
        return false;
    }

    void* graph = nullptr;
    void* instance = nullptr;
    void* layout = nullptr;
    int32_t layoutOff = -1;
    {
        std::lock_guard lock(g_cacheMutex);
        graph = g_graph;
        instance = g_instance;
        layout = g_layout;
        layoutOff = g_layoutPtrOff;
    }
    if (!graph || !instance)
    {
        if (outErr)
            *outErr = "no-cache";
        return false;
    }

    if (!layout && layoutOff >= 0)
    {
        SehPtr readPtr{static_cast<char*>(graph) + layoutOff, nullptr};
        if (Bridge_SehRun(&ReadPtrThunk, &readPtr))
            layout = readPtr.value;
    }
    if (!layout)
    {
        if (outErr)
            *outErr = "no-layout";
        return false;
    }

    const RuntimeInputInfo* sample = nullptr;
    static const char* kBootProps[] = {"verticalSpeed", "speed", "isMoving", "state", "deltaTime", nullptr};
    static const char* kGroups[] = {"playerLocomotion", "Locomotion", "Input", "MovePolicy", nullptr};
    for (const char* propName : kBootProps)
    {
        if (!propName || sample)
            break;
        for (const char* groupName : kGroups)
        {
            if (!groupName)
                break;
            if (TryReadInfo(layout, RED4ext::CName(groupName), RED4ext::CName(propName), &sample))
                break;
        }
    }

    std::vector<RuntimeInputInfo> inputs;
    if (sample)
    {
        if (!CollectInputsArray(layout, sample, inputs))
        {
            if (outErr)
                *outErr = "no-inputs-array";
            return false;
        }
    }
    else
    {
        for (int32_t byteOffset = 0; byteOffset <= 0x100; byteOffset += 8)
        {
            SehDyn dynContext{layout, byteOffset, nullptr, 0};
            if (!Bridge_SehRun(&DynThunk, &dynContext) || !dynContext.entries || dynContext.size == 0 ||
                dynContext.size > 4096)
                continue;
            SehType typeContext{dynContext.entries, EAnimInputType::InputUnknown};
            if (!Bridge_SehRun(&TypeThunk, &typeContext))
                continue;
            const auto typeIndex = static_cast<int32_t>(typeContext.type);
            if (typeIndex < 0 || typeIndex > 8)
                continue;
            sample = &dynContext.entries[0];
            if (CollectInputsArray(layout, sample, inputs))
                break;
            sample = nullptr;
            inputs.clear();
        }
        if (inputs.empty())
        {
            if (outErr)
                *outErr = "empty-inputs";
            return false;
        }
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::pair<std::string, std::string>>>
        featuresByGroup;
    for (const auto& inputInfo : inputs)
    {
        std::string type;
        std::string value;
        if (!ReadSlotJson(instance, inputInfo, type, value))
            continue;
        const char* groupName = SafeCName(inputInfo.groupName);
        const char* propName = SafeCName(inputInfo.inputName);
        if (!groupName || !*groupName || !propName || !*propName)
            continue;
        featuresByGroup[groupName][propName] = {type, value};
    }

    if (featuresByGroup.empty())
    {
        if (outErr)
            *outErr = "no-readable-slots";
        return false;
    }

    std::ostringstream out;
    out << '{';
    bool firstFeat = true;
    for (const auto& [featureName, properties] : featuresByGroup)
    {
        if (!firstFeat)
            out << ',';
        firstFeat = false;
        out << '"' << JsonEscape(featureName) << "\":{";
        bool firstProp = true;
        for (const auto& [propertyName, typeAndValue] : properties)
        {
            if (!firstProp)
                out << ',';
            firstProp = false;
            out << '"' << JsonEscape(propertyName) << "\":{\"type\":\"" << JsonEscape(typeAndValue.first)
                << "\",\"value\":" << typeAndValue.second << '}';
        }
        out << '}';
    }
    out << '}';
    outFeaturesObject = out.str();
    return true;
}

namespace
{
struct RtSlot
{
    uint32_t offset{~0u};
    uint32_t capacity{~0u};
};

struct SehRt
{
    void* container{};
    int32_t off{};
    RtSlot slot{};
};
void RtThunk(void* userData)
{
    auto* context = static_cast<SehRt*>(userData);
    auto* containerBase = static_cast<char*>(context->container) + context->off;
    context->slot.offset = *reinterpret_cast<uint32_t*>(containerBase);
    context->slot.capacity = *reinterpret_cast<uint32_t*>(containerBase + 4);
}

struct SehHandlePtr
{
    void* handleAddr{};
    void* ptr{};
};
void HandlePtrThunk(void* userData)
{
    auto* context = static_cast<SehHandlePtr*>(userData);
    context->ptr = *reinterpret_cast<void**>(context->handleAddr);
}

struct SehName
{
    void* varObj{};
    RED4ext::CName name{};
};
void NameThunk(void* userData)
{
    auto* context = static_cast<SehName*>(userData);
    context->name = *reinterpret_cast<RED4ext::CName*>(static_cast<char*>(context->varObj) + 0x30);
}

struct SehDynArr
{
    void* container{};
    int32_t arrOff{};
    void* entries{};
    uint32_t size{};
};
void DynArrThunk(void* userData)
{
    auto* context = static_cast<SehDynArr*>(userData);
    auto* containerBase = static_cast<char*>(context->container) + context->arrOff;
    context->entries = *reinterpret_cast<void**>(containerBase);
    context->size = *reinterpret_cast<uint32_t*>(containerBase + 8);
}

struct SehGraphVars
{
    void* graph{};
    void* container{};
};
void GraphVarsThunk(void* userData)
{
    auto* context = static_cast<SehGraphVars*>(userData);
    // AnimGraph::variables Handle at +0x58
    context->container = *reinterpret_cast<void**>(static_cast<char*>(context->graph) + 0x58);
}

bool ProbeRtSlot(void* container, uint32_t expectCap, uint32_t elemSize, uint32_t bufSize, RtSlot& out)
{
    if (!container || expectCap == 0 || expectCap > 4096)
        return false;
    for (int32_t byteOffset = 0x98; byteOffset + 8 <= 0x248; byteOffset += 4)
    {
        SehRt rtContext{container, byteOffset, {}};
        if (!Bridge_SehRun(&RtThunk, &rtContext))
            continue;
        if (rtContext.slot.capacity != expectCap || rtContext.slot.offset == ~0u)
            continue;
        const uint64_t requiredBytes = static_cast<uint64_t>(rtContext.slot.offset) +
                                       static_cast<uint64_t>(expectCap) * elemSize + 4ull;
        if (requiredBytes > bufSize)
            continue;
        out = rtContext.slot;
        return true;
    }
    return false;
}

bool CollectVarNamesAt(void* container, int32_t arrOff, std::vector<std::string>& names, uint32_t& outSize)
{
    names.clear();
    outSize = 0;
    SehDynArr dynArrContext{container, arrOff, nullptr, 0};
    if (!Bridge_SehRun(&DynArrThunk, &dynArrContext) || !dynArrContext.entries || dynArrContext.size == 0 ||
        dynArrContext.size > 4096)
        return false;
    if (!IsLikelyHeapPtr(dynArrContext.entries))
        return false;
    outSize = dynArrContext.size;
    names.resize(dynArrContext.size);
    uint32_t validNameCount = 0;
    for (uint32_t index = 0; index < dynArrContext.size; ++index)
    {
        void* handleAddr = static_cast<char*>(dynArrContext.entries) + static_cast<size_t>(index) * 16u;
        SehHandlePtr handlePtrContext{handleAddr, nullptr};
        if (!Bridge_SehRun(&HandlePtrThunk, &handlePtrContext) || !IsLikelyHeapPtr(handlePtrContext.ptr))
            continue;
        SehName nameContext{handlePtrContext.ptr, {}};
        if (!Bridge_SehRun(&NameThunk, &nameContext) || !nameContext.name.hash)
            continue;
        const char* variableName = SafeCName(nameContext.name);
        if (!variableName || !*variableName)
            continue;
        names[index] = variableName;
        ++validNameCount;
    }
    return validNameCount > 0;
}

bool AppendTypedVars(std::ostringstream& out, bool& first, void* buf, const RtSlot& slot, uint32_t elemSize,
                     const std::vector<std::string>& names, const char* typeTag, const auto& formatValue)
{
    if (slot.offset == ~0u || names.empty())
        return false;
    const uint32_t nameCount = static_cast<uint32_t>(names.size());
    if (nameCount > slot.capacity)
        return false;
    char* slotBase = static_cast<char*>(buf) + slot.offset;
    bool didWrite = false;
    for (uint32_t index = 0; index < nameCount; ++index)
    {
        if (names[index].empty())
            continue;
        const char* elementBytes = slotBase + static_cast<size_t>(index) * elemSize;
        if (!Bridge_IsReadable(elementBytes, elemSize))
            continue;
        std::string value;
        if (!formatValue(elementBytes, value))
            continue;
        if (!first)
            out << ',';
        first = false;
        didWrite = true;
        out << '"' << JsonEscape(names[index]) << "\":{\"type\":\"" << typeTag << "\",\"value\":" << value << '}';
    }
    return didWrite;
}

void* ResolveVarContainer(void* graph)
{
    if (!IsLikelyHeapPtr(graph))
        return nullptr;
    SehGraphVars graphVarsContext{graph, nullptr};
    if (!Bridge_SehRun(&GraphVarsThunk, &graphVarsContext) || !IsLikelyHeapPtr(graphVarsContext.container))
        return nullptr;
    return graphVarsContext.container;
}

// POD twin of StaticArray<CName,32> — no C++ dtor inside SEH frames.
struct NameArray32
{
    RED4ext::CName entries[32]{};
    uint32_t size{0};
};
} // namespace

bool InstanceDump_TryBuildVarsJson(std::string& outVarsObject, const char** outErr)
{
    if (outErr)
        *outErr = nullptr;

    void* graph = nullptr;
    void* instance = nullptr;
    {
        std::lock_guard lock(g_cacheMutex);
        graph = g_graph;
        instance = g_instance;
    }
    if (!IsLikelyHeapPtr(graph) || !IsLikelyHeapPtr(instance))
    {
        if (outErr)
            *outErr = "no-cache";
        return false;
    }

    SehBuf bufferContext{instance, nullptr, 0};
    if (!Bridge_SehRun(&BufThunk, &bufferContext) || !bufferContext.data ||
        !Bridge_IsReadable(bufferContext.data, bufferContext.size > 16 ? 16 : bufferContext.size))
    {
        if (outErr)
            *outErr = "no-buffer";
        return false;
    }

    void* container = ResolveVarContainer(graph);
    if (!container)
    {
        if (outErr)
            *outErr = "no-var-container";
        return false;
    }

    std::ostringstream out;
    out << '{';
    bool first = true;
    bool wroteAny = false;

    auto dumpType = [&](int32_t arrOff, uint32_t elemSize, const char* typeTag, const auto& formatValue) {
        std::vector<std::string> names;
        uint32_t arraySize = 0;
        if (!CollectVarNamesAt(container, arrOff, names, arraySize))
            return;
        RtSlot slot{};
        if (!ProbeRtSlot(container, arraySize, elemSize, bufferContext.size, slot))
            return;
        wroteAny |= AppendTypedVars(out, first, bufferContext.data, slot, elemSize, names, typeTag, formatValue);
    };

    // AnimVariableContainer DynArray offsets (RED4ext SDK).
    dumpType(0x38, 1, "bool", [](const char* elementBytes, std::string& valueOut) {
        valueOut = (*reinterpret_cast<const uint8_t*>(elementBytes) != 0) ? "true" : "false";
        return true;
    });
    dumpType(0x48, 4, "int", [](const char* elementBytes, std::string& valueOut) {
        int32_t intValue = 0;
        std::memcpy(&intValue, elementBytes, 4);
        valueOut = std::to_string(intValue);
        return true;
    });
    dumpType(0x58, 4, "float", [](const char* elementBytes, std::string& valueOut) {
        float floatScratch[4]{};
        if (!Bridge_TryReadF4(reinterpret_cast<const float*>(elementBytes), floatScratch))
            return false;
        char formatBuffer[64];
        std::snprintf(formatBuffer, sizeof(formatBuffer), "%.9g", static_cast<double>(floatScratch[0]));
        valueOut = formatBuffer;
        return true;
    });
    dumpType(0x68, 16, "vector", [](const char* elementBytes, std::string& valueOut) {
        float components[4]{};
        if (!Bridge_TryReadF4(reinterpret_cast<const float*>(elementBytes), components))
            return false;
        char formatBuffer[128];
        std::snprintf(formatBuffer, sizeof(formatBuffer), "{\"x\":%.9g,\"y\":%.9g,\"z\":%.9g,\"w\":%.9g}",
                      components[0], components[1], components[2], components[3]);
        valueOut = formatBuffer;
        return true;
    });
    dumpType(0x78, 16, "quat", [](const char* elementBytes, std::string& valueOut) {
        float components[4]{};
        if (!Bridge_TryReadF4(reinterpret_cast<const float*>(elementBytes), components))
            return false;
        char formatBuffer[128];
        std::snprintf(formatBuffer, sizeof(formatBuffer), "{\"x\":%.9g,\"y\":%.9g,\"z\":%.9g,\"w\":%.9g}",
                      components[0], components[1], components[2], components[3]);
        valueOut = formatBuffer;
        return true;
    });

    out << '}';
    if (!wroteAny)
    {
        if (outErr)
            *outErr = "no-readable-vars";
        return false;
    }
    outVarsObject = out.str();
    return true;
}

bool InstanceDump_TryBuildWrappersJson(void* animationController, std::string& outWrappersObject, const char** outErr)
{
    if (outErr)
        *outErr = nullptr;
    if (!IsLikelyHeapPtr(animationController))
    {
        if (outErr)
            *outErr = "no-acc";
        return false;
    }

    using Acc = RED4ext::ent::AnimationControllerComponent;
    using GetDataContext_t = const void* (*)(const void* /* const Handle<Acc>& */);
    using GatherActive_t = void (*)(const void* /*AnimDataContext&*/, NameArray32*);

    static GetDataContext_t getDataContextFn{};
    static GatherActive_t gatherActiveFn{};
    static std::atomic<bool> functionsReady{false};
    if (!functionsReady.load(std::memory_order_acquire))
    {
        getDataContextFn = reinterpret_cast<GetDataContext_t>(
            RED4ext::UniversalRelocBase::Resolve(AddressLib::ACC_GetDataContext));
        gatherActiveFn = reinterpret_cast<GatherActive_t>(
            RED4ext::UniversalRelocBase::Resolve(AddressLib::Anim_TEMP_GatherActiveVariables));
        functionsReady.store(true, std::memory_order_release);
    }
    if (!getDataContextFn || !gatherActiveFn)
    {
        if (outErr)
            *outErr = "resolve-wrapper-fns";
        return false;
    }

    struct FakeHandle
    {
        void* instance{};
        void* refCount{};
    };
    struct SehWrap
    {
        GetDataContext_t getCtx{};
        GatherActive_t gather{};
        FakeHandle handle{};
        const void* ctx{};
        NameArray32 names{};
        bool ok{false};
    };
    auto thunk = [](void* userData) {
        auto* wrapContext = static_cast<SehWrap*>(userData);
        wrapContext->ctx = wrapContext->getCtx(&wrapContext->handle);
        if (!wrapContext->ctx || !IsLikelyHeapPtr(wrapContext->ctx))
            return;
        wrapContext->names = {};
        wrapContext->gather(wrapContext->ctx, &wrapContext->names);
        if (wrapContext->names.size > 32)
            wrapContext->names.size = 32;
        wrapContext->ok = true;
    };

    SehWrap wrapContext{getDataContextFn, gatherActiveFn, FakeHandle{animationController, nullptr}, nullptr, {},
                        false};
    if (!Bridge_SehRun(+thunk, &wrapContext) || !wrapContext.ok)
    {
        if (outErr)
            *outErr = "gather-failed";
        return false;
    }

    std::ostringstream out;
    out << '{';
    bool first = true;
    for (uint32_t index = 0; index < wrapContext.names.size; ++index)
    {
        const char* variableName = SafeCName(wrapContext.names.entries[index]);
        if (!variableName || !*variableName)
            continue;
        if (!first)
            out << ',';
        first = false;
        out << '"' << JsonEscape(variableName) << "\":1";
    }
    out << '}';
    outWrappersObject = out.str();
    return true;
}
}
