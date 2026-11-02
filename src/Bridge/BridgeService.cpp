#include "Bridge/BridgeService.hpp"

#include "Bridge/Addresses.hpp"
#include "Bridge/BridgeOps.hpp"
#include "Bridge/CNameUtil.hpp"
#include "Bridge/CustomParam/Serialize.hpp"
#include "Bridge/FeatureSerialize.hpp"
#include "Bridge/InstanceBufferDump.hpp"
#include "Bridge/JsonEscape.hpp"
#include "Bridge/Seh.h"

#include <RED4ext/Scripting/Natives/Generated/cp/PlayerSystem.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/AnimWrapperWeightSetter.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/Object.hpp>
#include <RED4ext/Scripting/Natives/entEntity.hpp>

#include <RedLib.hpp>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstring>

namespace Bridge
{
namespace
{
std::string g_cfgHost = "127.0.0.1";
uint16_t g_cfgPort = 29770;

using Acc = RED4ext::ent::AnimationControllerComponent;

// All const& args: keep as C++ refs so MinHook trampoline can tail-call (const T* DEP'd).
using ApplyFeature_t = void (*)(Acc*, RED4ext::CName, const RED4ext::Handle<RED4ext::IScriptable>&);
using SetInputFloat_t = void (*)(Acc*, RED4ext::CName, float);
using SetInputBool_t = void (*)(Acc*, RED4ext::CName, bool);
using SetInputInt32_t = void (*)(Acc*, RED4ext::CName, int32_t);
using SetInputVector4_t = void (*)(Acc*, RED4ext::CName, const RED4ext::Vector4&);
using SetInputQuat_t = void (*)(Acc*, RED4ext::CName, const RED4ext::Quaternion&);
using PushEvent_t = void (*)(Acc*, RED4ext::CName);
using PushCustomParam_t = void (*)(Acc*, const RED4ext::Handle<RED4ext::ISerializable>&, bool);
using SetAnimWrapper_t = void (*)(Acc*, RED4ext::CName, float);
using OnSetWrapperWeight_t = void (*)(Acc*, const RED4ext::ent::AnimWrapperWeightSetter&);
using TempSetVarV4_t = bool (*)(void* /*AnimGraph*/, void* /*AnimInstanceBuffer&*/, RED4ext::CName,
                                 const RED4ext::Vector4&);
using TempSetVarQuat_t = bool (*)(void* /*AnimGraph*/, void* /*AnimInstanceBuffer&*/, RED4ext::CName,
                                   const RED4ext::Quaternion&);
using AnimGraphApplyFeature_t = bool (*)(void* /*AnimGraph*/, void* /*AnimInstanceBuffer&*/, RED4ext::CName,
                                         const void* /* const AnimFeature* */);
using OnPlayerSpawned_t = void* (*)(void*, void*, void*, void*);

ApplyFeature_t g_origApplyFeature{};
SetInputFloat_t g_origSetInputFloat{};
SetInputBool_t g_origSetInputBool{};
SetInputInt32_t g_origSetInputInt32{};
SetInputVector4_t g_origSetInputVector4{};
SetInputQuat_t g_origSetInputQuat{};
PushEvent_t g_origPushEvent{};
PushCustomParam_t g_origPushCustomParam{};
SetAnimWrapper_t g_origSetAnimWrapper{};
OnSetWrapperWeight_t g_origOnSetWrapperWeight{};
TempSetVarV4_t g_origTempSetVarV4{};
TempSetVarQuat_t g_origTempSetVarQuat{};
AnimGraphApplyFeature_t g_origAnimGraphApplyFeature{};
OnPlayerSpawned_t g_origOnPlayerSpawned{};

void* g_addrApplyFeature{};
void* g_addrSetInputFloat{};
void* g_addrSetInputBool{};
void* g_addrSetInputInt32{};
void* g_addrSetInputVector4{};
void* g_addrSetInputQuat{};
void* g_addrPushEvent{};
void* g_addrPushCustomParam{};
void* g_addrSetAnimWrapper{};
void* g_addrOnSetWrapperWeight{};
void* g_addrTempSetVarV4{};
void* g_addrTempSetVarQuat{};
void* g_addrAnimGraphApplyFeature{};
void* g_addrOnPlayerSpawned{};

struct ShadowBoard
{
    // feature -> property -> {type, valueJson}
    std::unordered_map<std::string, std::unordered_map<std::string, std::pair<std::string, std::string>>> features;
    // var name -> {type, valueJson}
    std::unordered_map<std::string, std::pair<std::string, std::string>> vars;
    // wrapper name -> weight json
    std::unordered_map<std::string, std::string> wrappers;
};

std::mutex g_shadowMutex;
std::unordered_map<uint64_t, ShadowBoard> g_boardsByEntity;

std::mutex g_queueMutex;
std::deque<std::string> g_queue;
std::condition_variable g_queueCv;
std::atomic<bool> g_needSnapshot{false};

/** ACC PushEvents since last tick — flushed atomically on op:tick (same frame as Update). */
std::mutex g_frameEventsMutex;
std::vector<std::string> g_frameExternalEvents;

// config.json prewatch — who to accumulate from load; stream = who TCP follows.
bool g_cfgWatchPlayer = true;
std::unordered_set<uint64_t> g_cfgEntityIds;
bool g_cfgStreamPlayer = true;
uint64_t g_cfgStreamEntityId = 0; // used when !g_cfgStreamPlayer

std::mutex g_watchMutex;
std::unordered_map<Acc*, uint64_t> g_accToEntity; // resolved watched ACCs
std::atomic<Acc*> g_streamAcc{nullptr};
std::atomic<uint64_t> g_streamEntityId{0};
std::atomic<uint64_t> g_playerEntityId{0}; // from cpPlayerSystem, not IsA(PlayerPuppet)
std::atomic<bool> g_sessionAttached{false};
std::atomic<bool> g_clientConnected{false};
/** ACC ApplyFeature group hash awaiting matching AnimGraph::ApplyFeature (stream only). */
std::atomic<uint64_t> g_pendingStreamFeatureHash{0};

// For probe logs from hooks.
RED4ext::v1::PluginHandle g_logHandle{};
const RED4ext::v1::Sdk* g_logSdk{};
std::atomic<int> g_vecProbeLeft{24};
std::atomic<int> g_quatProbeLeft{12};
std::atomic<int> g_tempV4ProbeLeft{32};
std::atomic<int> g_tempQuatProbeLeft{16};
std::atomic<int> g_floatHeartbeatLeft{5};
std::atomic<int> g_featProbeLeft{24};
std::atomic<int> g_customProbeLeft{48};
std::atomic<uint32_t> g_floatCalls{0};
std::atomic<uint32_t> g_vecCalls{0};
std::atomic<uint32_t> g_customCalls{0};

std::wstring PluginDir()
{
    HMODULE moduleHandle{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&PluginDir), &moduleHandle);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(moduleHandle, path, MAX_PATH);
    std::wstring modulePath(path);
    const auto slash = modulePath.find_last_of(L"\\/");
    return slash == std::wstring::npos ? modulePath : modulePath.substr(0, slash + 1);
}

uint64_t ParseEntityIdJson(const nlohmann::json& value)
{
    if (value.is_number_unsigned() || value.is_number_integer())
        return value.get<uint64_t>();
    if (value.is_string())
    {
        const auto& text = value.get_ref<const std::string&>();
        if (text.empty())
            return 0;
        char* parseEnd = nullptr;
        const unsigned long long parsed = (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
                                              ? std::strtoull(text.c_str(), &parseEnd, 16)
                                              : std::strtoull(text.c_str(), &parseEnd, 10);
        if (parseEnd && *parseEnd == '\0')
            return static_cast<uint64_t>(parsed);
    }
    return 0;
}

// RedLib pattern (same as RHT GetPlayer → entity:GetEntityID().hash):
// GetGameSystem<cpPlayerSystem>()->GetLocalPlayerControlledGameObject()->entityID
uint64_t ResolvePlayerEntityId()
{
    // Always re-query — entity id changes menu→session; Acc map caches the match.
    auto* system = Red::GetGameSystem<RED4ext::cp::PlayerSystem>();
    if (!system)
        return g_playerEntityId.load(std::memory_order_relaxed);

    RED4ext::Handle<RED4ext::GameObject> player;
    if (!Red::CallVirtual(system, "GetLocalPlayerControlledGameObject", player) || !player)
        return 0;

    auto* entity = static_cast<RED4ext::ent::Entity*>(player.GetPtr());
    if (!entity)
        return 0;

    const uint64_t entityId = entity->entityID.hash;
    if (entityId)
        g_playerEntityId.store(entityId, std::memory_order_relaxed);
    return entityId;
}

void ApplyPrewatchJson(const nlohmann::json& prewatch)
{
    if (prewatch.contains("player") && prewatch["player"].is_boolean())
        g_cfgWatchPlayer = prewatch["player"].get<bool>();

    if (prewatch.contains("entityIds") && prewatch["entityIds"].is_array())
    {
        for (const auto& item : prewatch["entityIds"])
        {
            const uint64_t entityId = ParseEntityIdJson(item);
            if (entityId)
                g_cfgEntityIds.insert(entityId);
        }
    }

    if (!prewatch.contains("stream"))
        return;

    if (prewatch["stream"].is_string())
    {
        const auto stream = prewatch["stream"].get<std::string>();
        if (stream == "player")
        {
            g_cfgStreamPlayer = true;
            return;
        }
    }

    const uint64_t streamEntityId = ParseEntityIdJson(prewatch["stream"]);
    if (!streamEntityId)
        return;

    g_cfgStreamPlayer = false;
    g_cfgStreamEntityId = streamEntityId;
    g_cfgEntityIds.insert(streamEntityId);
}

void LoadPluginConfig()
{
    g_cfgHost = "127.0.0.1";
    g_cfgPort = 29770;
    g_cfgWatchPlayer = true;
    g_cfgEntityIds.clear();
    g_cfgStreamPlayer = true;
    g_cfgStreamEntityId = 0;

    const std::wstring path = PluginDir() + L"config.json";
    std::ifstream file(path);
    if (!file)
    {
        if (g_logSdk && g_logSdk->logger)
            g_logSdk->logger->Info(g_logHandle, "AnimgraphBridge: config.json missing — defaults (127.0.0.1:29770, player-only)");
        return;
    }

    nlohmann::json root;
    try
    {
        file >> root;
    }
    catch (const std::exception& parseError)
    {
        if (g_logSdk && g_logSdk->logger)
        {
            char msg[192];
            std::snprintf(msg, sizeof(msg), "AnimgraphBridge: config.json parse failed: %s", parseError.what());
            g_logSdk->logger->Error(g_logHandle, msg);
        }
        return;
    }

    if (root.contains("host") && root["host"].is_string())
    {
        const auto host = root["host"].get<std::string>();
        if (!host.empty())
            g_cfgHost = host;
    }

    if (root.contains("port") && root["port"].is_number_integer())
    {
        const int port = root["port"].get<int>();
        if (port > 0 && port <= 65535)
            g_cfgPort = static_cast<uint16_t>(port);
    }

    if (root.contains("prewatch") && root["prewatch"].is_object())
        ApplyPrewatchJson(root["prewatch"]);
    else
        ApplyPrewatchJson(root); // flat fallback (legacy prewatch-shaped root)

    if (g_logSdk && g_logSdk->logger)
    {
        char msg[256];
        std::snprintf(msg, sizeof(msg), "AnimgraphBridge: config host=%s port=%u prewatch player=%d entityIds=%zu stream=%s",
                      g_cfgHost.c_str(), static_cast<unsigned>(g_cfgPort), g_cfgWatchPlayer ? 1 : 0,
                      g_cfgEntityIds.size(), g_cfgStreamPlayer ? "player" : "entityId");
        g_logSdk->logger->Info(g_logHandle, msg);
    }
}

struct WatchCtx
{
    Acc* self{};
    bool matched{};
    uint64_t entityId{};
    bool isPlayer{};
};

void WatchThunk(void* rawCtx)
{
    auto* watch = static_cast<WatchCtx*>(rawCtx);
    auto* self = watch->self;
    if (!self || !self->owner)
        return;

    const uint64_t entityId = self->owner->entityID.hash;
    if (!entityId)
        return;

    bool isPlayer = false;
    if (g_cfgWatchPlayer)
    {
        const uint64_t playerEntityId = ResolvePlayerEntityId();
        isPlayer = playerEntityId != 0 && entityId == playerEntityId;
    }

    const bool idWatch = g_cfgEntityIds.find(entityId) != g_cfgEntityIds.end();
    if (!isPlayer && !idWatch)
        return;

    watch->matched = true;
    watch->entityId = entityId;
    watch->isPlayer = isPlayer;
}

// Resolve watch; updates per-entity ACC map. Does not clear boards (write-history only).
bool WatchAcc(Acc* self, uint64_t* outEntityId = nullptr)
{
    if (!self)
        return false;

    {
        std::lock_guard lock(g_watchMutex);
        const auto found = g_accToEntity.find(self);
        if (found != g_accToEntity.end())
        {
            if (outEntityId)
                *outEntityId = found->second;
            return true;
        }
    }

    WatchCtx ctx{self, false, 0, false};
    Bridge_SehRun(&WatchThunk, &ctx);
    if (!ctx.matched)
        return false;

    {
        std::lock_guard lock(g_watchMutex);
        g_accToEntity[self] = ctx.entityId;
    }

    if (g_cfgStreamPlayer && ctx.isPlayer)
    {
        g_streamAcc.store(self, std::memory_order_relaxed);
        g_streamEntityId.store(ctx.entityId, std::memory_order_relaxed);
    }
    else if (!g_cfgStreamPlayer && ctx.entityId == g_cfgStreamEntityId)
    {
        g_streamAcc.store(self, std::memory_order_relaxed);
        g_streamEntityId.store(ctx.entityId, std::memory_order_relaxed);
    }

    if (outEntityId)
        *outEntityId = ctx.entityId;
    return true;
}

bool IsStreamAcc(Acc* self)
{
    return self && self == g_streamAcc.load(std::memory_order_relaxed);
}

void Enqueue(std::string line)
{
    {
        std::lock_guard lock(g_queueMutex);
        constexpr size_t kMax = 4096;
        if (g_queue.size() >= kMax)
            g_queue.pop_front();
        g_queue.push_back(std::move(line));
    }
    g_queueCv.notify_one();
}

bool ShadowVarIfChanged(uint64_t entityId, const std::string& name, const std::string& type, const std::string& valueJson)
{
    std::lock_guard lock(g_shadowMutex);
    auto& shadowEntry = g_boardsByEntity[entityId].vars[name];
    if (shadowEntry.first == type && shadowEntry.second == valueJson)
        return false;
    shadowEntry = {type, valueJson};
    return true;
}

bool ShadowFeatureIfChanged(uint64_t entityId, const std::string& feature, const std::string& property,
                            const std::string& type, const std::string& valueJson)
{
    std::lock_guard lock(g_shadowMutex);
    auto& shadowEntry = g_boardsByEntity[entityId].features[feature][property];
    if (shadowEntry.first == type && shadowEntry.second == valueJson)
        return false;
    shadowEntry = {type, valueJson};
    return true;
}

bool ShadowWrapperIfChanged(uint64_t entityId, const std::string& name, const std::string& valueJson)
{
    std::lock_guard lock(g_shadowMutex);
    auto& shadowEntry = g_boardsByEntity[entityId].wrappers[name];
    if (shadowEntry == valueJson)
        return false;
    shadowEntry = valueJson;
    return true;
}

void CaptureWrapper(Acc* self, RED4ext::CName name, float value)
{
    uint64_t entityId = 0;
    if (!WatchAcc(self, &entityId))
        return;

    char valBuf[64];
    std::snprintf(valBuf, sizeof(valBuf), "%.9g", static_cast<double>(value));
    const std::string wrapName = SafeCName(name);
    if (!ShadowWrapperIfChanged(entityId, wrapName, valBuf))
        return;
    if (!IsStreamAcc(self))
        return;

    Enqueue(MakeSetWrapper(wrapName, valBuf));
}

std::string BuildSnapshotJson()
{
    const uint64_t entityId = g_streamEntityId.load(std::memory_order_relaxed);
    Acc* streamAcc = g_streamAcc.load(std::memory_order_relaxed);

    std::string instanceFeatures;
    std::string instanceVars;
    std::string instanceWrappers;
    const char* featuresDumpError = nullptr;
    const char* varsDumpError = nullptr;
    const char* wrappersDumpError = nullptr;
    const bool haveFeatures = InstanceDump_TryBuildFeaturesJson(instanceFeatures, &featuresDumpError);
    const bool haveVars = InstanceDump_TryBuildVarsJson(instanceVars, &varsDumpError);
    const bool haveWrappers = InstanceDump_TryBuildWrappersJson(streamAcc, instanceWrappers, &wrappersDumpError);
    if (g_logSdk && g_logSdk->logger)
    {
        char msg[256];
        std::snprintf(msg, sizeof(msg),
                      "AnimgraphBridge: snapshot features=%s vars=%s wrappers=%s",
                      haveFeatures ? "instance" : (featuresDumpError ? featuresDumpError : "shadow"),
                      haveVars ? "instance" : (varsDumpError ? varsDumpError : "shadow"),
                      haveWrappers ? "instance" : (wrappersDumpError ? wrappersDumpError : "shadow"));
        g_logSdk->logger->Info(g_logHandle, msg);
    }

    std::lock_guard lock(g_shadowMutex);
    const ShadowBoard empty{};
    const ShadowBoard* board = &empty;
    if (entityId)
    {
        const auto boardEntry = g_boardsByEntity.find(entityId);
        if (boardEntry != g_boardsByEntity.end())
            board = &boardEntry->second;
    }

    const char* source = (haveFeatures && haveVars && haveWrappers)   ? "instance"
                         : (haveFeatures || haveVars || haveWrappers) ? "mixed"
                                                                      : "shadow";

    std::ostringstream out;
    out << "{\"op\":\"" << Op::kSnapshot << "\",\"entity\":\"";
    if (entityId)
        out << entityId;
    else
        out << "none";
    out << "\",\"source\":\"" << source << "\",\"features\":";

    if (haveFeatures)
        out << instanceFeatures;
    else
    {
        out << '{';
        bool firstFeat = true;
        for (const auto& [featureName, properties] : board->features)
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
    }

    out << ",\"vars\":";
    if (haveVars)
        out << instanceVars;
    else
    {
        out << '{';
        bool firstVar = true;
        for (const auto& [varName, typeAndValue] : board->vars)
        {
            if (!firstVar)
                out << ',';
            firstVar = false;
            out << '"' << JsonEscape(varName) << "\":{\"type\":\"" << JsonEscape(typeAndValue.first)
                << "\",\"value\":" << typeAndValue.second << '}';
        }
        out << '}';
    }

    out << ",\"wrappers\":";
    if (haveWrappers)
        out << instanceWrappers;
    else
    {
        out << '{';
        bool firstWrap = true;
        for (const auto& [wrapperName, weightJson] : board->wrappers)
        {
            if (!firstWrap)
                out << ',';
            firstWrap = false;
            out << '"' << JsonEscape(wrapperName) << "\":" << weightJson;
        }
        out << '}';
    }
    out << '}';
    return out.str();
}

bool OnAnimGraphApplyFeature(void* graph, void* instance, RED4ext::CName group, const void* feature)
{
    const bool applied = g_origAnimGraphApplyFeature(graph, instance, group, feature);
    const uint64_t pending = g_pendingStreamFeatureHash.load(std::memory_order_relaxed);
    const bool isPendingStreamFeature = pending != 0 && pending == group.hash;
    if (isPendingStreamFeature)
        g_pendingStreamFeatureHash.store(0, std::memory_order_relaxed);
    InstanceDump_OnAnimGraphApplyFeature(graph, instance, group,
                                         static_cast<const RED4ext::IScriptable*>(feature), applied,
                                         isPendingStreamFeature);
    return applied;
}

void OnApplyFeature(Acc* self, RED4ext::CName name, const RED4ext::Handle<RED4ext::IScriptable>& feature)
{
    uint64_t entityId = 0;
    const bool watched = WatchAcc(self, &entityId);
    if (watched && IsStreamAcc(self))
        g_pendingStreamFeatureHash.store(name.hash, std::memory_order_relaxed);

    g_origApplyFeature(self, name, feature);

    if (!watched)
        return;

    auto* featureInstance = feature.GetPtr();
    if (!featureInstance)
        return;

    const std::string featName = SafeCName(name);
    int propertyCount = 0;
    int changedPropertyCount = 0;
    int vectorQuatPropertyCount = 0;
    std::ostringstream propsJson;
    propsJson << '[';

    ForEachAnimFeatureProperty(featureInstance,
                               [&](const std::string& propertyName, const std::string& type,
                                   const std::string& valueJson) {
                                   ++propertyCount;
                                   if (type == "vector" || type == "quat")
                                       ++vectorQuatPropertyCount;
                                   if (!ShadowFeatureIfChanged(entityId, featName, propertyName, type, valueJson))
                                       return;

                                   if (changedPropertyCount > 0)
                                       propsJson << ',';
                                   propsJson << "{\"name\":\"" << JsonEscape(propertyName) << "\",\"type\":\"" << type
                                             << "\",\"value\":" << valueJson << '}';
                                   ++changedPropertyCount;
                               });
    propsJson << ']';

    if (changedPropertyCount > 0 && IsStreamAcc(self))
        Enqueue(MakeSetFeature(featName, propsJson.str()));

    if (g_featProbeLeft.fetch_sub(1, std::memory_order_relaxed) > 0 && g_logSdk && g_logSdk->logger)
    {
        char msg[160];
        std::snprintf(msg, sizeof(msg), "AnimgraphBridge: ApplyFeature name=%s props=%d changed=%d vec/quat=%d",
                      featName.c_str(), propertyCount, changedPropertyCount, vectorQuatPropertyCount);
        g_logSdk->logger->Info(g_logHandle, msg);
    }
}

void OnSetInputFloat(Acc* self, RED4ext::CName name, float value)
{
    g_origSetInputFloat(self, name, value);
    const uint32_t callCount = g_floatCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (g_floatHeartbeatLeft.fetch_sub(1, std::memory_order_relaxed) > 0 && g_logSdk && g_logSdk->logger)
    {
        char msg[160];
        std::snprintf(msg, sizeof(msg), "AnimgraphBridge: SetInputFloat alive n=%u name=%s", callCount,
                      SafeCName(name));
        g_logSdk->logger->Info(g_logHandle, msg);
    }
    uint64_t entityId = 0;
    if (!WatchAcc(self, &entityId))
        return;

    const std::string varName = SafeCName(name);
    char valBuf[64];
    std::snprintf(valBuf, sizeof(valBuf), "%.9g", static_cast<double>(value));
    if (!ShadowVarIfChanged(entityId, varName, "float", valBuf))
        return;
    if (!IsStreamAcc(self))
        return;

    Enqueue(MakeSetVar(varName, "float", valBuf));
}

void OnSetInputBool(Acc* self, RED4ext::CName name, bool value)
{
    g_origSetInputBool(self, name, value);
    uint64_t entityId = 0;
    if (!WatchAcc(self, &entityId))
        return;

    const std::string varName = SafeCName(name);
    const char* boolJson = value ? "true" : "false";
    if (!ShadowVarIfChanged(entityId, varName, "bool", boolJson))
        return;
    if (!IsStreamAcc(self))
        return;

    Enqueue(MakeSetVar(varName, "bool", boolJson));
}

void OnSetInputInt32(Acc* self, RED4ext::CName name, int32_t value)
{
    g_origSetInputInt32(self, name, value);
    uint64_t entityId = 0;
    if (!WatchAcc(self, &entityId))
        return;

    const std::string varName = SafeCName(name);
    char valBuf[32];
    std::snprintf(valBuf, sizeof(valBuf), "%d", value);
    if (!ShadowVarIfChanged(entityId, varName, "int", valBuf))
        return;
    if (!IsStreamAcc(self))
        return;

    Enqueue(MakeSetVar(varName, "int", valBuf));
}

void ProbeF4(const char* kind, Acc* self, RED4ext::CName name, const float* valuePtr, std::atomic<int>& probeBudget)
{
    if (probeBudget.fetch_sub(1, std::memory_order_relaxed) <= 0)
        return;
    if (!g_logSdk || !g_logSdk->logger)
        return;

    const bool stream = IsStreamAcc(self);
    float components[4]{};
    const bool readOk = Bridge_TryReadF4(valuePtr, components);
    char msg[192];
    if (readOk)
    {
        std::snprintf(msg, sizeof(msg), "AnimgraphBridge: %s name=%s stream=%d read=%.3g,%.3g,%.3g,%.3g", kind,
                      SafeCName(name), stream ? 1 : 0, components[0], components[1], components[2], components[3]);
    }
    else
    {
        std::snprintf(msg, sizeof(msg), "AnimgraphBridge: %s name=%s stream=%d read=FAIL ptr=%p", kind, SafeCName(name),
                      stream ? 1 : 0, static_cast<const void*>(valuePtr));
    }
    g_logSdk->logger->Info(g_logHandle, msg);
}

void CaptureF4Var(Acc* self, RED4ext::CName name, const float components[4], const char* type)
{
    // Hot path: only Acc* already registered by cooler hooks (no RTTI here).
    uint64_t entityId = 0;
    {
        std::lock_guard lock(g_watchMutex);
        const auto found = g_accToEntity.find(self);
        if (found == g_accToEntity.end())
            return;
        entityId = found->second;
    }

    char valBuf[128];
    std::snprintf(valBuf, sizeof(valBuf), "{\"x\":%.9g,\"y\":%.9g,\"z\":%.9g,\"w\":%.9g}",
                  static_cast<double>(components[0]), static_cast<double>(components[1]),
                  static_cast<double>(components[2]), static_cast<double>(components[3]));
    const std::string varName = SafeCName(name);
    if (!ShadowVarIfChanged(entityId, varName, type, valBuf))
        return;
    if (!IsStreamAcc(self))
        return;
    Enqueue(MakeSetVar(varName, type, valBuf));
}

void OnSetInputVector4(Acc* self, RED4ext::CName name, const RED4ext::Vector4& value)
{
    g_vecCalls.fetch_add(1, std::memory_order_relaxed);
    // Read BEFORE orig — after-call &value/name can be clobbered (probe showed ptr=0x10/None).
    const float* valuePtr = reinterpret_cast<const float*>(&value);
    ProbeF4("SetInputVector4", self, name, valuePtr, g_vecProbeLeft);

    float components[4]{};
    const bool readOk = Bridge_TryReadF4(valuePtr, components);
    const RED4ext::CName nameCopy = name;

    g_origSetInputVector4(self, name, value);

    if (readOk)
        CaptureF4Var(self, nameCopy, components, "vector");
}

void OnSetInputQuat(Acc* self, RED4ext::CName name, const RED4ext::Quaternion& value)
{
    const float* valuePtr = reinterpret_cast<const float*>(&value);
    ProbeF4("SetInputQuat", self, name, valuePtr, g_quatProbeLeft);

    float components[4]{};
    const bool readOk = Bridge_TryReadF4(valuePtr, components);
    const RED4ext::CName nameCopy = name;

    g_origSetInputQuat(self, name, value);

    if (readOk)
        CaptureF4Var(self, nameCopy, components, "quat");
}

void OnPushEvent(Acc* self, RED4ext::CName name)
{
    g_origPushEvent(self, name);
    if (!WatchAcc(self) || !IsStreamAcc(self))
        return;

    const char* raw = SafeCName(name);
    if (!raw || !*raw)
        return;
    // Buffer until OnGameTick — editor must see events on the same publish as this frame's dt.
    std::lock_guard lock(g_frameEventsMutex);
    for (const auto& bufferedEvent : g_frameExternalEvents)
    {
        if (bufferedEvent == raw)
            return;
    }
    g_frameExternalEvents.emplace_back(raw);
}

struct CustomTypeCtx
{
    RED4ext::ISerializable* serializable{};
    const char* typeName{"null"};
};

void CustomTypeThunk(void* rawCtx)
{
    auto* customCtx = static_cast<CustomTypeCtx*>(rawCtx);
    if (!customCtx->serializable)
        return;
    auto* serializableClass = customCtx->serializable->GetType();
    if (!serializableClass)
        return;
    customCtx->typeName = SafeCName(serializableClass->name);
}

void OnPushCustomParam(Acc* self, const RED4ext::Handle<RED4ext::ISerializable>& data, bool flag)
{
    g_origPushCustomParam(self, data, flag);

    const uint32_t callCount = g_customCalls.fetch_add(1, std::memory_order_relaxed) + 1;

    CustomTypeCtx ctx{data.GetPtr(), "null"};
    Bridge_SehRun(&CustomTypeThunk, &ctx);

    const bool watched = WatchAcc(self);
    const bool stream = watched && IsStreamAcc(self);

    if (g_customProbeLeft.fetch_sub(1, std::memory_order_relaxed) > 0 && g_logSdk && g_logSdk->logger)
    {
        char msg[192];
        std::snprintf(msg, sizeof(msg), "AnimgraphBridge: PushCustomParam n=%u type=%s flag=%d stream=%d inst=%p",
                      callCount, ctx.typeName, flag ? 1 : 0, stream ? 1 : 0,
                      static_cast<const void*>(ctx.serializable));
        g_logSdk->logger->Info(g_logHandle, msg);
    }

    if (!stream)
        return;

    auto event = SerializeCustomParam(data.GetPtr());
    if (!event)
        return;

    Enqueue(MakeServiceEvent(event->dump()));
}

void OnSetAnimWrapperVariable(Acc* self, RED4ext::CName name, float value)
{
    g_origSetAnimWrapper(self, name, value);
    CaptureWrapper(self, name, value);
}

void OnSetWrapperWeight(Acc* self, const RED4ext::ent::AnimWrapperWeightSetter& setter)
{
    g_origOnSetWrapperWeight(self, setter);
    // Event path — may also call SetAnimWrapperVariable; shadow dedupes.
    CaptureWrapper(self, setter.key, setter.value);
}

void ProbeTempF4(const char* kind, RED4ext::CName name, const float* valuePtr, std::atomic<int>& probeBudget)
{
    if (probeBudget.fetch_sub(1, std::memory_order_relaxed) <= 0)
        return;
    if (!g_logSdk || !g_logSdk->logger)
        return;

    float components[4]{};
    const bool readOk = Bridge_TryReadF4(valuePtr, components);
    char msg[192];
    if (readOk)
    {
        std::snprintf(msg, sizeof(msg), "AnimgraphBridge: %s name=%s read=%.3g,%.3g,%.3g,%.3g", kind, SafeCName(name),
                      components[0], components[1], components[2], components[3]);
    }
    else
    {
        std::snprintf(msg, sizeof(msg), "AnimgraphBridge: %s name=%s read=FAIL ptr=%p", kind, SafeCName(name),
                      static_cast<const void*>(valuePtr));
    }
    g_logSdk->logger->Info(g_logHandle, msg);
}

bool OnTempSetVarV4(void* graph, void* buffer, RED4ext::CName name, const RED4ext::Vector4& value)
{
    const float* valuePtr = reinterpret_cast<const float*>(&value);
    ProbeTempF4("TEMP_SetVariable_V4", name, valuePtr, g_tempV4ProbeLeft);
    const bool applied = g_origTempSetVarV4(graph, buffer, name, value);
    InstanceDump_OnTempSetVariable(graph, buffer);
    return applied;
}

bool OnTempSetVarQuat(void* graph, void* buffer, RED4ext::CName name, const RED4ext::Quaternion& value)
{
    const float* valuePtr = reinterpret_cast<const float*>(&value);
    ProbeTempF4("TEMP_SetVariable_Quat", name, valuePtr, g_tempQuatProbeLeft);
    const bool applied = g_origTempSetVarQuat(graph, buffer, name, value);
    InstanceDump_OnTempSetVariable(graph, buffer);
    return applied;
}

void* OnPlayerSpawnedHook(void* arg0, void* arg1, void* arg2, void* arg3)
{
    void* returnValue = g_origOnPlayerSpawned(arg0, arg1, arg2, arg3);
    BridgeService::Get().OnSessionReady("playerSpawned");
    return returnValue;
}

void ClearSessionBindings()
{
    g_streamAcc.store(nullptr, std::memory_order_relaxed);
    g_streamEntityId.store(0, std::memory_order_relaxed);
    {
        std::lock_guard lock(g_watchMutex);
        g_accToEntity.clear();
    }
    {
        std::lock_guard lock(g_shadowMutex);
        g_boardsByEntity.clear();
    }
    {
        std::lock_guard lock(g_frameEventsMutex);
        g_frameExternalEvents.clear();
    }
    InstanceDump_Clear();
    g_pendingStreamFeatureHash.store(0, std::memory_order_relaxed);
    g_playerEntityId.store(0, std::memory_order_relaxed);
}

void* ResolveDefaultEA(uintptr_t defaultEA)
{
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!base)
        return nullptr;
    return reinterpret_cast<void*>(base + (defaultEA - AddressLib::PreferredImageBase));
}

template<typename T>
bool AttachAt(const RED4ext::v1::Sdk* sdk, RED4ext::v1::PluginHandle handle, void* hookAddress, T detour, T* original,
              void** addrOut)
{
    if (!hookAddress)
        return false;
    *addrOut = hookAddress;
    return sdk->hooking->Attach(handle, hookAddress, reinterpret_cast<void*>(detour),
                                reinterpret_cast<void**>(original));
}

template<typename T>
bool Attach(const RED4ext::v1::Sdk* sdk, RED4ext::v1::PluginHandle handle, uint32_t hash, T detour, T* original,
            void** addrOut)
{
    const auto hookAddress = reinterpret_cast<void*>(RED4ext::UniversalRelocBase::Resolve(hash));
    return AttachAt(sdk, handle, hookAddress, detour, original, addrOut);
}
} // namespace

BridgeService& BridgeService::Get()
{
    static BridgeService instance;
    return instance;
}

void BridgeService::OnSessionReady(const char* reason)
{
    ClearSessionBindings();
    g_sessionAttached.store(true, std::memory_order_relaxed);
    const uint64_t entityId = ResolvePlayerEntityId();
    g_needSnapshot.store(true, std::memory_order_relaxed);
    if (m_sdk && m_sdk->logger)
    {
        char msg[160];
        std::snprintf(msg, sizeof(msg), "AnimgraphBridge: session ready (%s) playerEntityId=%llu",
                      reason ? reason : "?", static_cast<unsigned long long>(entityId));
        m_sdk->logger->Info(m_handle, msg);
    }
}

void BridgeService::OnSessionEnded(const char* reason)
{
    g_sessionAttached.store(false, std::memory_order_relaxed);
    ClearSessionBindings();
    if (m_sdk && m_sdk->logger)
    {
        char msg[128];
        std::snprintf(msg, sizeof(msg), "AnimgraphBridge: session ended (%s)", reason ? reason : "?");
        m_sdk->logger->Info(m_handle, msg);
    }
}

void BridgeService::OnGameTick(float deltaTime, float rawDeltaTime)
{
    if (!g_clientConnected.load(std::memory_order_relaxed))
        return;
    if (!g_sessionAttached.load(std::memory_order_relaxed))
        return;
    if (!(deltaTime > 0.f) && !(rawDeltaTime > 0.f))
        return;

    std::vector<std::string> events;
    {
        std::lock_guard lock(g_frameEventsMutex);
        events.swap(g_frameExternalEvents);
    }

    Enqueue(MakeTick(static_cast<double>(deltaTime),
                     static_cast<double>(rawDeltaTime > 0.f ? rawDeltaTime : deltaTime), events));
}

void BridgeService::Start(RED4ext::v1::PluginHandle handle, const RED4ext::v1::Sdk* sdk)
{
    m_handle = handle;
    m_sdk = sdk;
    g_logHandle = handle;
    g_logSdk = sdk;
    g_vecProbeLeft.store(24, std::memory_order_relaxed);
    g_quatProbeLeft.store(12, std::memory_order_relaxed);
    g_tempV4ProbeLeft.store(32, std::memory_order_relaxed);
    g_tempQuatProbeLeft.store(16, std::memory_order_relaxed);
    g_floatHeartbeatLeft.store(5, std::memory_order_relaxed);
    g_featProbeLeft.store(24, std::memory_order_relaxed);
    g_customProbeLeft.store(48, std::memory_order_relaxed);
    g_floatCalls.store(0, std::memory_order_relaxed);
    g_vecCalls.store(0, std::memory_order_relaxed);
    g_customCalls.store(0, std::memory_order_relaxed);

    LoadPluginConfig();

    WSADATA wsaData{};
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (!InstallHooks())
    {
        if (m_sdk && m_sdk->logger)
            m_sdk->logger->Error(m_handle, "AnimgraphBridge: failed to install one or more hooks");
        return;
    }

    m_running = true;
    m_worker = std::thread([this] { WorkerLoop(); });

    if (m_sdk && m_sdk->logger)
    {
        char msg[160];
        std::snprintf(msg, sizeof(msg), "AnimgraphBridge: hooks installed; listening on %s:%u", g_cfgHost.c_str(),
                      static_cast<unsigned>(g_cfgPort));
        m_sdk->logger->Info(m_handle, msg);
    }
}

void BridgeService::Stop()
{
    m_running = false;
    g_queueCv.notify_all();
    if (m_worker.joinable())
        m_worker.join();

    UninstallHooks();
    g_streamAcc.store(nullptr, std::memory_order_relaxed);
    g_streamEntityId.store(0, std::memory_order_relaxed);
    {
        std::lock_guard lock(g_watchMutex);
        g_accToEntity.clear();
    }
    {
        std::lock_guard lock(g_shadowMutex);
        g_boardsByEntity.clear();
    }
    g_playerEntityId.store(0, std::memory_order_relaxed);
    WSACleanup();
}

bool BridgeService::InstallHooks()
{
    bool allHooksAttached = true;
    // ApplyFeature: probe names only (no RTTI prop walk). Vector4 SetInput via DefaultEA.
    allHooksAttached &= Attach(m_sdk, m_handle, AddressLib::ACC_ApplyFeature, &OnApplyFeature, &g_origApplyFeature,
                               &g_addrApplyFeature);
    allHooksAttached &= Attach(m_sdk, m_handle, AddressLib::ACC_SetInput_Float, &OnSetInputFloat, &g_origSetInputFloat,
                               &g_addrSetInputFloat);
    allHooksAttached &= Attach(m_sdk, m_handle, AddressLib::ACC_SetInput_Bool, &OnSetInputBool, &g_origSetInputBool,
                               &g_addrSetInputBool);
    allHooksAttached &= Attach(m_sdk, m_handle, AddressLib::ACC_SetInput_Int32, &OnSetInputInt32, &g_origSetInputInt32,
                               &g_addrSetInputInt32);

    {
        void* vecAddr = ResolveDefaultEA(AddressLib::ACC_SetInput_Vector4_DefaultEA);
        if (m_sdk && m_sdk->logger)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "AnimgraphBridge: SetInputVector4 attach at %p (hash collision bypass)",
                          vecAddr);
            m_sdk->logger->Info(m_handle, msg);
        }
        allHooksAttached &=
            AttachAt(m_sdk, m_handle, vecAddr, &OnSetInputVector4, &g_origSetInputVector4, &g_addrSetInputVector4);
    }

    allHooksAttached &= Attach(m_sdk, m_handle, AddressLib::ACC_SetInput_Quaternion, &OnSetInputQuat, &g_origSetInputQuat,
                               &g_addrSetInputQuat);
    allHooksAttached &= Attach(m_sdk, m_handle, AddressLib::ACC_PushEvent, &OnPushEvent, &g_origPushEvent,
                               &g_addrPushEvent);
    allHooksAttached &= Attach(m_sdk, m_handle, AddressLib::ACC_PushCustomParam, &OnPushCustomParam,
                               &g_origPushCustomParam, &g_addrPushCustomParam);
    allHooksAttached &= Attach(m_sdk, m_handle, AddressLib::ACC_SetAnimWrapperVariable, &OnSetAnimWrapperVariable,
                               &g_origSetAnimWrapper, &g_addrSetAnimWrapper);
    allHooksAttached &= Attach(m_sdk, m_handle, AddressLib::ACC_OnSetWrapperWeight, &OnSetWrapperWeight,
                               &g_origOnSetWrapperWeight, &g_addrOnSetWrapperWeight);
    allHooksAttached &= Attach(m_sdk, m_handle, AddressLib::AnimGraph_TEMP_SetVariable_Vector4, &OnTempSetVarV4,
                               &g_origTempSetVarV4, &g_addrTempSetVarV4);
    allHooksAttached &= Attach(m_sdk, m_handle, AddressLib::AnimGraph_TEMP_SetVariable_Quaternion, &OnTempSetVarQuat,
                               &g_origTempSetVarQuat, &g_addrTempSetVarQuat);
    allHooksAttached &= Attach(m_sdk, m_handle, AddressLib::AnimGraph_ApplyFeature, &OnAnimGraphApplyFeature,
                               &g_origAnimGraphApplyFeature, &g_addrAnimGraphApplyFeature);
    allHooksAttached &= Attach(m_sdk, m_handle, AddressLib::PlayerSystem_OnPlayerSpawned, &OnPlayerSpawnedHook,
                               &g_origOnPlayerSpawned, &g_addrOnPlayerSpawned);
    return allHooksAttached;
}

void BridgeService::UninstallHooks()
{
    if (!m_sdk)
        return;

    auto detach = [&](void* hookAddress) {
        if (hookAddress)
            m_sdk->hooking->Detach(m_handle, hookAddress);
    };

    detach(g_addrApplyFeature);
    detach(g_addrSetInputFloat);
    detach(g_addrSetInputBool);
    detach(g_addrSetInputInt32);
    detach(g_addrSetInputVector4);
    detach(g_addrSetInputQuat);
    detach(g_addrPushEvent);
    detach(g_addrPushCustomParam);
    detach(g_addrSetAnimWrapper);
    detach(g_addrOnSetWrapperWeight);
    detach(g_addrTempSetVarV4);
    detach(g_addrTempSetVarQuat);
    detach(g_addrAnimGraphApplyFeature);
    detach(g_addrOnPlayerSpawned);
}

void BridgeService::WorkerLoop()
{
    SOCKET listenSock = INVALID_SOCKET;
    SOCKET clientSock = INVALID_SOCKET;
    bool sentSnapshot = false;

    auto closeClient = [&]() {
        if (clientSock != INVALID_SOCKET)
        {
            closesocket(clientSock);
            clientSock = INVALID_SOCKET;
        }
        sentSnapshot = false;
        g_clientConnected.store(false, std::memory_order_relaxed);
    };

    auto closeListen = [&]() {
        closeClient();
        if (listenSock != INVALID_SOCKET)
        {
            closesocket(listenSock);
            listenSock = INVALID_SOCKET;
        }
    };

    auto bindListen = [&]() -> bool {
        closeListen();
        listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSock == INVALID_SOCKET)
            return false;

        BOOL reuse = TRUE;
        setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in bindAddress{};
        bindAddress.sin_family = AF_INET;
        bindAddress.sin_port = htons(g_cfgPort);
        inet_pton(AF_INET, g_cfgHost.c_str(), &bindAddress.sin_addr);

        if (bind(listenSock, reinterpret_cast<sockaddr*>(&bindAddress), sizeof(bindAddress)) != 0)
        {
            closeListen();
            return false;
        }
        if (listen(listenSock, 1) != 0)
        {
            closeListen();
            return false;
        }

        u_long nonBlocking = 1;
        ioctlsocket(listenSock, FIONBIO, &nonBlocking);
        return true;
    };

    auto sendLine = [&](const std::string& line) -> bool {
        if (clientSock == INVALID_SOCKET)
            return false;
        std::string payload = line;
        payload.push_back('\n');
        size_t sent = 0;
        while (sent < payload.size())
        {
            const int bytesSent =
                send(clientSock, payload.data() + sent, static_cast<int>(payload.size() - sent), 0);
            if (bytesSent <= 0)
            {
                closeClient();
                return false;
            }
            sent += static_cast<size_t>(bytesSent);
        }
        return true;
    };

    while (m_running && !bindListen())
    {
        if (m_sdk && m_sdk->logger)
        {
            char msg[160];
            std::snprintf(msg, sizeof(msg), "AnimgraphBridge: bind %s:%u failed; retrying…", g_cfgHost.c_str(),
                          static_cast<unsigned>(g_cfgPort));
            m_sdk->logger->Warn(m_handle, msg);
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    while (m_running)
    {
        // Accept new client (replaces previous if any).
        {
            sockaddr_in peer{};
            int peerLen = sizeof(peer);
            const SOCKET accepted =
                accept(listenSock, reinterpret_cast<sockaddr*>(&peer), &peerLen);
            if (accepted != INVALID_SOCKET)
            {
                closeClient();
                clientSock = accepted;
                u_long nonBlocking = 0;
                ioctlsocket(clientSock, FIONBIO, &nonBlocking);
                g_clientConnected.store(true, std::memory_order_relaxed);
                g_needSnapshot = true;
                sentSnapshot = false;
            }
        }

        if (clientSock == INVALID_SOCKET)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (g_needSnapshot.exchange(false) || !sentSnapshot)
        {
            if (!sendLine(BuildSnapshotJson()))
                continue;
            sentSnapshot = true;
        }

        std::string line;
        {
            std::unique_lock lock(g_queueMutex);
            g_queueCv.wait_for(lock, std::chrono::milliseconds(100),
                               [&] { return !g_queue.empty() || !m_running || g_needSnapshot; });
            if (!g_queue.empty())
            {
                line = std::move(g_queue.front());
                g_queue.pop_front();
            }
        }

        if (!line.empty() && !sendLine(line))
            continue;
    }

    closeListen();
}
}
