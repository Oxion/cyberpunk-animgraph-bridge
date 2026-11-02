#pragma once

#include <RedLib.hpp>
#include <RED4ext/SystemUpdate.hpp>

namespace Bridge
{
// RedLib-registered game system: session lifecycle + per-frame tick → editor.
class BridgeSessionSystem : public Red::IGameSystem
{
public:
    void OnWorldAttached(Red::world::RuntimeScene* aScene) override;
    void OnBeforeWorldDetach(Red::world::RuntimeScene* aScene) override;
    bool OnGameRestored() override;
    void OnRegisterUpdates(Red::UpdateRegistrar* aRegistrar) override;

    RTTI_IMPL_TYPEINFO(BridgeSessionSystem);
    RTTI_IMPL_ALLOCATOR();

private:
    void OnUpdateTick(Red::FrameInfo& aFrame, Red::JobQueue& aJobQueue);
};
}
