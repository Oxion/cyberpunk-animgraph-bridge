#pragma once

#include <atomic>
#include <thread>

#include <RED4ext/RED4ext.hpp>

namespace Bridge
{
class BridgeService
{
public:
    static BridgeService& Get();

    void Start(RED4ext::v1::PluginHandle aHandle, const RED4ext::v1::Sdk* aSdk);
    void Stop();

    // Clear ACC/stream caches and re-resolve player entity id (save load / spawn).
    void OnSessionReady(const char* reason);
    void OnSessionEnded(const char* reason);

    /** End-of-frame pulse for live editor (1 game Update → 1 sim step). */
    void OnGameTick(float deltaTime, float rawDeltaTime);

private:
    BridgeService() = default;

    bool InstallHooks();
    void UninstallHooks();
    void WorkerLoop();

    RED4ext::v1::PluginHandle m_handle{};
    const RED4ext::v1::Sdk* m_sdk{};

    std::atomic<bool> m_running{false};
    std::thread m_worker;
};
}
