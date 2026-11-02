#pragma once

#include <cstdint>

namespace Bridge::AddressLib
{
// Adler-32 hashes of confirmed MSVC mangled names (cp2077-symbols / cyberpunk2077_addresses.json).
// Game 2.5 (file 3.0.88.46235) — hashes revalidated against local addresses.json.

constexpr uint32_t ACC_ApplyFeature = 316482401u;
constexpr uint32_t ACC_SetInput_Float = 3216184875u;
constexpr uint32_t ACC_SetInput_Bool = 3598063243u;
constexpr uint32_t ACC_SetInput_Int32 = 3215201830u;
// COLLISION: hash 3828226594 hits two symbols (0001:001c89f4 AND 0001:029b5a7c).
// Do NOT Resolve() this hash — use ACC_SetInput_Vector4_DefaultEA + ASLR.
constexpr uint32_t ACC_SetInput_Vector4 = 3828226594u;
constexpr uint32_t ACC_SetInput_Quaternion = 953293729u;
constexpr uint32_t ACC_PushEvent = 3247117892u;
// Service-event inject (LookAtParams_*, MixerSlot*, IKTarget*, …).
constexpr uint32_t ACC_PushCustomParam = 1384980888u;
constexpr uint32_t ACC_SetAnimWrapperVariable = 808393639u;
constexpr uint32_t ACC_OnSetWrapperWeight = 1172775487u;
// Accepted graph writes (flush). Unique hashes — Vector/Quat ACC SetInput may be cold.
constexpr uint32_t AnimGraph_TEMP_SetVariable_Vector4 = 1580932708u;
constexpr uint32_t AnimGraph_TEMP_SetVariable_Quaternion = 3214286819u;

// Instance-buffer readback (snapshot).
constexpr uint32_t AnimGraph_ApplyFeature = 2588352315u; // 0001:001b5108
// ?FindInputEntry@AnimInstanceBufferLayout@anim@@QEBAPEBURuntimeInputInfo@12@VCName@@0_N@Z
constexpr uint32_t AnimInstanceBufferLayout_FindInputEntry = 3193118675u; // 0001:001b6b18
// GetVarsBufferData/Size are inlined — read AnimInstanceBuffer::{m_varsBuffer@0, m_varsBufferSize@8}.
// Graph var writes (Float/Bool/Int) — also useful for cache warm.
constexpr uint32_t AnimGraph_TEMP_SetVariable_Float = 111680109u; // 0001:00b57d2c
constexpr uint32_t AnimGraph_TEMP_SetVariable_Bool = 564992717u;  // 0001:00ef0f60
constexpr uint32_t AnimGraph_TEMP_SetVariable_Int32 = 110697064u; // 0001:026ee8f4
// Wrapper active dump: ACC → AnimDataContext → TEMP_GatherActiveVariables.
constexpr uint32_t ACC_GetDataContext = 2989043910u;              // 0001:029b5600
constexpr uint32_t Anim_TEMP_GatherActiveVariables = 3042779548u; // 0001:00c50d8c

// Preferred image base 0x140000000; section1 RVA = 0x1000 + file offset (addresses.json).
// Correct Vector4 SetInput body (sibling of Int32/Quat at 029b5xxx): 0x140000000+0x1000+0x029b5a7c.
constexpr uintptr_t PreferredImageBase = 0x140000000ull;
constexpr uintptr_t ACC_SetInput_Vector4_DefaultEA = 0x1429B6A7Cull;

// cp::PlayerSystem::OnPlayerMainObjectSpawned — after load / new game player ready.
constexpr uint32_t PlayerSystem_OnPlayerSpawned = 2050111212u;
}
