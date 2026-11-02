#include "Bridge/BridgeService.hpp"
#include "Bridge/BridgeSessionSystem.hpp"

#include "App/Project.hpp"

#include <RedLib.hpp>

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle aHandle, RED4ext::v1::EMainReason aReason,
                                        const RED4ext::v1::Sdk* aSdk)
{
    switch (aReason)
    {
    case RED4ext::v1::EMainReason::Load:
        Red::TypeInfoRegistrar::RegisterDiscovered();
        Bridge::BridgeService::Get().Start(aHandle, aSdk);
        break;
    case RED4ext::v1::EMainReason::Unload:
        Bridge::BridgeService::Get().Stop();
        break;
    }

    return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = App::Project::NameW;
    aInfo->author = App::Project::AuthorW;
    aInfo->version = RED4EXT_V1_SEMVER(App::Project::Version.major, App::Project::Version.minor,
                                       App::Project::Version.patch);
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_LATEST; // 2.5
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1_COMPAT_0;
}
