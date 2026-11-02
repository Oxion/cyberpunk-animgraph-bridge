#include "Bridge/BridgeSessionSystem.hpp"

#include "Bridge/BridgeService.hpp"

#include <RED4ext/SystemUpdate-inl.hpp>

namespace Bridge
{
void BridgeSessionSystem::OnWorldAttached(Red::world::RuntimeScene*)
{
    BridgeService::Get().OnSessionReady("worldAttached");
}

void BridgeSessionSystem::OnBeforeWorldDetach(Red::world::RuntimeScene*)
{
    BridgeService::Get().OnSessionEnded("worldDetach");
}

bool BridgeSessionSystem::OnGameRestored()
{
    BridgeService::Get().OnSessionReady("gameRestored");
    return true;
}

void BridgeSessionSystem::OnRegisterUpdates(Red::UpdateRegistrar* aRegistrar)
{
    // After camera/aim wrote AnimFeature_Camera deltas for this frame.
    aRegistrar->RegisterUpdate(Red::UpdateTickGroup::PostPlayerAimUpdate, this, "AnimgraphBridge/Tick",
                               {this, &BridgeSessionSystem::OnUpdateTick});
}

void BridgeSessionSystem::OnUpdateTick(Red::FrameInfo& aFrame, Red::JobQueue&)
{
    float deltaTime = aFrame.deltaTime;
    float rawDeltaTime = deltaTime;
    if (aFrame.details)
    {
        rawDeltaTime = aFrame.details->deltaTime;
        // timeDilation-scaled dt is what anim graphs typically see as timeDelta.
        if (aFrame.details->timeDilation > 0.f)
            deltaTime = rawDeltaTime * aFrame.details->timeDilation;
    }
    BridgeService::Get().OnGameTick(deltaTime, rawDeltaTime);
}
}

RTTI_DEFINE_CLASS(Bridge::BridgeSessionSystem);
