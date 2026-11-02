#pragma once



#include <RED4ext/CName.hpp>

#include <RED4ext/Scripting/IScriptable.hpp>



#include <string>



namespace Bridge

{

/** Cache graph+instance when AnimGraph::ApplyFeature applies a stream-watched feature. */

void InstanceDump_OnAnimGraphApplyFeature(void* graph, void* instance, RED4ext::CName group,

                                          const RED4ext::IScriptable* feature, bool applied,

                                          bool streamCandidate);



/** Keep cache warm when TEMP_SetVariable hits the same graph/instance. */

void InstanceDump_OnTempSetVariable(void* graph, void* instance);



void InstanceDump_Clear();



/**

 * Build features{} JSON object body from AnimInstanceBuffer when cache+layout resolve.

 * Returns false → caller should use shadow features.

 */

bool InstanceDump_TryBuildFeaturesJson(std::string& outFeaturesObject, const char** outErr);



/** Graph variables from AnimVariableContainer + vars buffer. */

bool InstanceDump_TryBuildVarsJson(std::string& outVarsObject, const char** outErr);



/**

 * Active anim-wrapper variables (engine stores enable bits; emit as 1.0).

 * @param acc stream AnimationControllerComponent*

 */

bool InstanceDump_TryBuildWrappersJson(void* animationController, std::string& outWrappersObject, const char** outErr);

}


