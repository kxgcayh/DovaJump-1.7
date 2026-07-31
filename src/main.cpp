// main.cpp – DovaJump v0.9.1.3 (wall jump animation with sprint support)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <xmmintrin.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "SKSE/SKSE.h"
#include "SKSE/API.h"
#include "SKSE/Interfaces.h"
#include "SKSE/Trampoline.h"

#include "REL/Relocation.h"
#include "RE/Skyrim.h"
#include "RE/J/JumpHandler.h"
#include "RE/P/PlayerControls.h"
#include "RE/P/PlayerCamera.h"
#include "RE/B/bhkCharProxyController.h"
#include "RE/H/hkpCharacterProxy.h"
#include "RE/H/hkpRootCdPoint.h"
#include "RE/H/hkContactPoint.h"
#include "RE/T/TESHavokUtilities.h"
#include "logger.h"

extern "C" {
    __declspec(dllexport) constinit auto SKSEPlugin_Version = []() {
        SKSE::PluginVersionData v;
        v.pluginVersion = 24;
        v.PluginName("DovaJump");
        v.AuthorName("YourName");
        v.UsesAddressLibrary(true);
        v.UsesStructsPost629(true);
        v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST });
        return v;
        }();
}

namespace DovaJump
{
    using namespace std::chrono;

    // Settings
    static bool  g_modEnabled = true;
    static float g_chargeTime = 4.0f;
    static float g_maxMultiplier = 3.0f;
    static bool  g_enableNotifications = true;

    static bool  g_wallJumpEnabled = true;
    static float g_wallJumpHorizontalForce = 4.0f;
    static float g_wallJumpVerticalForce = 7.0f;
    static float g_wallJumpAngleThreshold = 45.0f;
    static float g_wallJumpCooldown = 0.4f;
    static float g_wallJumpReach = 150.0f;

    static float g_wMult = 1.5f;
    static float g_sMult = 1.5f;
    static float g_adMult = 1.3f;
    static float g_staggerForce = 100.0f;

    static bool  g_slopeJumpEnabled = true;
    static bool  g_obstacleBypassEnabled = false;

    static float g_assistDurationMs = 250.0f;
    static float g_assistKeepDistanceBoost = 0.1f;
    static float g_assistFrictionMult = 0.0f;
    static float g_assistMicroThreshold = 5.0f;

    static float g_ceilingMicroThreshold = 3.0f;
    static float g_ledgeMagnetMaxDepth = 15.0f;
    static float g_ledgeMagnetStrength = 80.0f;
    static bool  g_vaultSnapEnabled = true;
    static float g_vaultRequiredSpeed = 150.0f;

    static bool  g_npcCollisionResponse = true;
    static float g_npcPushStrength = 120.0f;
    static float g_npcPushVertical = 30.0f;
    static float g_playerRecoilStrength = 1.0f;
    static float g_collisionRadius = 100.0f;

    static float g_coyoteTimeWindow = 1.2f;
    static constexpr float kGravity = 600.0f;
    static constexpr float kNpcCollisionCooldown = 0.5f;
    static constexpr float kNpcMinDistance = 40.0f;
    static bool  g_verboseLogging = true;

    // State
    static std::atomic<bool>  g_isHolding{ false };
    static std::atomic<float> g_currentChargeMult{ 1.0f };
    static steady_clock::time_point g_holdStart;
    static steady_clock::time_point g_lastWallJump;
    static bool                     g_hasWallJumped = false;
    static bool                     g_wasOnSlope = false;

    static bool                     g_assistActive = false;
    static steady_clock::time_point g_assistStart;
    static float                    g_origKeepDistance = 0.05f;
    static float                    g_origStaticFriction = 0.5f;
    static float                    g_origDynamicFriction = 0.5f;
    static bool                     g_assistParamsSaved = false;

    static steady_clock::time_point g_lastGroundedTime;
    static bool                     g_wasGrounded = false;
    static steady_clock::time_point g_postJumpBlockUntil;   // The update lock is blocked after the jump

    // For dynamic hook management
    static RE::JumpHandler* g_handler = nullptr;
    static void** g_oldVtable = nullptr;
    static uintptr_t multCallSite = 0;
    static bool g_trampolineActive = false;

    static void** g_clonedVtable = nullptr;
    static size_t g_vtableSize = 0;

    using CanProcess_t = bool(RE::JumpHandler*, RE::InputEvent*);
    static CanProcess_t* g_originalCanProcess = nullptr;

    using ProcessButton_t = void(RE::JumpHandler*, RE::ButtonEvent*, RE::PlayerControlsData*);
    static ProcessButton_t* g_originalProcessButton = nullptr;

    using JumpMultFunc_t = float (*)();
    static inline JumpMultFunc_t g_originalMult = nullptr;
    static float MultiplyJump();   // ← add 0.9.1.3

    static uintptr_t ReadDisp(uintptr_t src) {
        return src + 5 + static_cast<uintptr_t>(*reinterpret_cast<int32_t*>(src + 5 - 4));
    }

    // Serialization (version 11)
    static void SaveSettings(SKSE::SerializationInterface* serde) {
        if (!serde->OpenRecord('DOJ_', 11)) return;
        serde->WriteRecordData(&g_modEnabled, sizeof(bool));
        serde->WriteRecordData(&g_chargeTime, sizeof(float));
        serde->WriteRecordData(&g_maxMultiplier, sizeof(float));
        serde->WriteRecordData(&g_enableNotifications, sizeof(bool));
        serde->WriteRecordData(&g_wallJumpEnabled, sizeof(bool));
        serde->WriteRecordData(&g_wallJumpHorizontalForce, sizeof(float));
        serde->WriteRecordData(&g_wallJumpVerticalForce, sizeof(float));
        serde->WriteRecordData(&g_wallJumpAngleThreshold, sizeof(float));
        serde->WriteRecordData(&g_wallJumpCooldown, sizeof(float));
        serde->WriteRecordData(&g_wallJumpReach, sizeof(float));
        serde->WriteRecordData(&g_wMult, sizeof(float));
        serde->WriteRecordData(&g_sMult, sizeof(float));
        serde->WriteRecordData(&g_adMult, sizeof(float));
        serde->WriteRecordData(&g_staggerForce, sizeof(float));
        float dummyWarp = 0.0f;
        serde->WriteRecordData(&dummyWarp, sizeof(float));
        serde->WriteRecordData(&g_slopeJumpEnabled, sizeof(bool));
        serde->WriteRecordData(&g_assistDurationMs, sizeof(float));
        serde->WriteRecordData(&g_assistKeepDistanceBoost, sizeof(float));
        serde->WriteRecordData(&g_assistFrictionMult, sizeof(float));
        serde->WriteRecordData(&g_assistMicroThreshold, sizeof(float));
        serde->WriteRecordData(&g_obstacleBypassEnabled, sizeof(bool));
        serde->WriteRecordData(&g_ceilingMicroThreshold, sizeof(float));
        serde->WriteRecordData(&g_ledgeMagnetMaxDepth, sizeof(float));
        serde->WriteRecordData(&g_ledgeMagnetStrength, sizeof(float));
        serde->WriteRecordData(&g_vaultSnapEnabled, sizeof(bool));
        serde->WriteRecordData(&g_vaultRequiredSpeed, sizeof(float));
        serde->WriteRecordData(&g_npcCollisionResponse, sizeof(bool));
        serde->WriteRecordData(&g_npcPushStrength, sizeof(float));
        serde->WriteRecordData(&g_npcPushVertical, sizeof(float));
        serde->WriteRecordData(&g_playerRecoilStrength, sizeof(float));
        serde->WriteRecordData(&g_collisionRadius, sizeof(float));
        serde->WriteRecordData(&g_coyoteTimeWindow, sizeof(float));
    }

    static void LoadSettings(SKSE::SerializationInterface* serde) {
        std::uint32_t type, ver, len;
        bool tmp_bool;
        float tmp_float;

        while (serde->GetNextRecordInfo(type, ver, len)) {
            if (type != 'DOJ_') continue;

            serde->ReadRecordData(&tmp_bool, sizeof(bool)); g_modEnabled = tmp_bool;
            serde->ReadRecordData(&tmp_float, sizeof(float));
            if (!std::isnan(tmp_float) && tmp_float >= 0.1f && tmp_float <= 10.0f) g_chargeTime = tmp_float;
            serde->ReadRecordData(&tmp_float, sizeof(float));
            if (!std::isnan(tmp_float) && tmp_float >= 1.0f && tmp_float <= 10.0f) g_maxMultiplier = tmp_float;
            serde->ReadRecordData(&tmp_bool, sizeof(bool)); g_enableNotifications = tmp_bool;

            if (ver >= 2) {
                serde->ReadRecordData(&tmp_bool, sizeof(bool)); g_wallJumpEnabled = tmp_bool;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 0.0f && tmp_float <= 10.0f) g_wallJumpHorizontalForce = tmp_float;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 0.0f && tmp_float <= 10.0f) g_wallJumpVerticalForce = tmp_float;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float)) g_wallJumpAngleThreshold = tmp_float;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float)) g_wallJumpCooldown = tmp_float;
            }

            if (ver >= 3) {
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float)) g_wallJumpReach = tmp_float;
            }

            if (ver >= 4) {
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 0.1f && tmp_float <= 3.0f) g_wMult = tmp_float;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 0.1f && tmp_float <= 3.0f) g_sMult = tmp_float;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 0.1f && tmp_float <= 3.0f) g_adMult = tmp_float;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 0.0f && tmp_float <= 100.0f) g_staggerForce = tmp_float;
            }

            if (ver >= 5) {
                serde->ReadRecordData(&tmp_float, sizeof(float));
            }

            if (ver >= 6) {
                serde->ReadRecordData(&tmp_bool, sizeof(bool));
                g_slopeJumpEnabled = tmp_bool;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 100.0f && tmp_float <= 500.0f)
                    g_assistDurationMs = tmp_float;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 0.0f && tmp_float <= 0.3f)
                    g_assistKeepDistanceBoost = tmp_float;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 0.0f && tmp_float <= 3.0f)
                    g_assistFrictionMult = tmp_float;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 1.0f && tmp_float <= 10.0f)
                    g_assistMicroThreshold = tmp_float;
            }

            if (ver >= 7) {
                serde->ReadRecordData(&tmp_bool, sizeof(bool));
                g_obstacleBypassEnabled = tmp_bool;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 1.0f && tmp_float <= 10.0f)
                    g_ceilingMicroThreshold = tmp_float;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 5.0f && tmp_float <= 30.0f)
                    g_ledgeMagnetMaxDepth = tmp_float;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 0.0f && tmp_float <= 200.0f)
                    g_ledgeMagnetStrength = tmp_float;
                serde->ReadRecordData(&tmp_bool, sizeof(bool));
                g_vaultSnapEnabled = tmp_bool;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 100.0f && tmp_float <= 300.0f)
                    g_vaultRequiredSpeed = tmp_float;
            }

            if (ver >= 8) {
                serde->ReadRecordData(&tmp_bool, sizeof(bool));
                g_npcCollisionResponse = tmp_bool;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 0.0f && tmp_float <= 150.0f)
                    g_npcPushStrength = tmp_float;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 0.0f && tmp_float <= 50.0f)
                    g_npcPushVertical = tmp_float;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 0.0f && tmp_float <= 20.0f)
                    g_playerRecoilStrength = tmp_float;
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 50.0f && tmp_float <= 150.0f)
                    g_collisionRadius = tmp_float;
            }

            if (ver >= 11) {
                serde->ReadRecordData(&tmp_float, sizeof(float));
                if (!std::isnan(tmp_float) && tmp_float >= 0.0f && tmp_float <= 3.0f)
                    g_coyoteTimeWindow = tmp_float;
            }
        }
    }

    // Papyrus API
    static void SetModEnabled(RE::StaticFunctionTag*, bool enable) {
        if (g_modEnabled == enable) return;
        g_modEnabled = enable;
        RE::DebugNotification(enable ? "DovaJump enabled" : "DovaJump disabled");

        // Managing the vtable hook
        if (g_handler && g_oldVtable && g_clonedVtable) {
            *reinterpret_cast<void***>(g_handler) = enable ? g_clonedVtable : g_oldVtable;
        }

        // MultiplyJump Controls
        if (enable && !g_trampolineActive) {
            auto& trampoline = SKSE::GetTrampoline();
            trampoline.write_call<5>(multCallSite, reinterpret_cast<uintptr_t>(&MultiplyJump));
            g_trampolineActive = true;
        }
        else if (!enable && g_trampolineActive) {
            auto& trampoline = SKSE::GetTrampoline();
            trampoline.write_call<5>(multCallSite, reinterpret_cast<uintptr_t>(g_originalMult));
            g_trampolineActive = false;
        }
    }
    static void SetChargeTime(RE::StaticFunctionTag*, float seconds) { if (seconds >= 0.1f && seconds <= 10.0f) g_chargeTime = seconds; }
    static void SetMaxMultiplier(RE::StaticFunctionTag*, float mult) { if (mult >= 1.0f && mult <= 10.0f) g_maxMultiplier = mult; }
    static void SetEnableNotifications(RE::StaticFunctionTag*, bool enable) { g_enableNotifications = enable; RE::DebugNotification(enable ? "Notifications ON" : "Notifications OFF"); }
    static float GetChargeTime(RE::StaticFunctionTag*) { return g_chargeTime; }
    static float GetMaxMultiplier(RE::StaticFunctionTag*) { return g_maxMultiplier; }
    static bool GetModEnabled(RE::StaticFunctionTag*) { return g_modEnabled; }
    static bool GetEnableNotifications(RE::StaticFunctionTag*) { return g_enableNotifications; }

    static void SetWallJumpEnabled(RE::StaticFunctionTag*, bool enable) { g_wallJumpEnabled = enable; }
    static void SetWallJumpHorizontalForce(RE::StaticFunctionTag*, float f) { if (f >= 0.0f && f <= 10.0f) g_wallJumpHorizontalForce = f; }
    static void SetWallJumpVerticalForce(RE::StaticFunctionTag*, float f) { if (f >= 0.0f && f <= 10.0f) g_wallJumpVerticalForce = f; }
    static void SetWallJumpAngleThreshold(RE::StaticFunctionTag*, float a) { if (a >= 0.0f && a <= 90.0f) g_wallJumpAngleThreshold = a; }
    static void SetWallJumpCooldown(RE::StaticFunctionTag*, float cd) { if (cd >= 0.0f && cd <= 5.0f) g_wallJumpCooldown = cd; }
    static void SetWallJumpReach(RE::StaticFunctionTag*, float r) { if (r >= 50.0f && r <= 300.0f) g_wallJumpReach = r; }
    static float GetWallJumpHorizontalForce(RE::StaticFunctionTag*) { return g_wallJumpHorizontalForce; }
    static float GetWallJumpVerticalForce(RE::StaticFunctionTag*) { return g_wallJumpVerticalForce; }
    static float GetWallJumpAngleThreshold(RE::StaticFunctionTag*) { return g_wallJumpAngleThreshold; }
    static float GetWallJumpCooldown(RE::StaticFunctionTag*) { return g_wallJumpCooldown; }
    static float GetWallJumpReach(RE::StaticFunctionTag*) { return g_wallJumpReach; }
    static bool  GetWallJumpEnabled(RE::StaticFunctionTag*) { return g_wallJumpEnabled; }

    static void SetWASDMult_W(RE::StaticFunctionTag*, float f) { if (f >= 0.1f && f <= 3.0f) g_wMult = f; }
    static void SetWASDMult_S(RE::StaticFunctionTag*, float f) { if (f >= 0.1f && f <= 3.0f) g_sMult = f; }
    static void SetWASDMult_AD(RE::StaticFunctionTag*, float f) { if (f >= 0.1f && f <= 3.0f) g_adMult = f; }
    static float GetWASDMult_W(RE::StaticFunctionTag*) { return g_wMult; }
    static float GetWASDMult_S(RE::StaticFunctionTag*) { return g_sMult; }
    static float GetWASDMult_AD(RE::StaticFunctionTag*) { return g_adMult; }
    static void SetStaggerForce(RE::StaticFunctionTag*, float f) { if (f >= 0.0f && f <= 100.0f) g_staggerForce = f; }
    static float GetStaggerForce(RE::StaticFunctionTag*) { return g_staggerForce; }

    static void SetSlopeJumpEnabled(RE::StaticFunctionTag*, bool enable) { g_slopeJumpEnabled = enable; }
    static bool GetSlopeJumpEnabled(RE::StaticFunctionTag*) { return g_slopeJumpEnabled; }

    static void SetObstacleBypassEnabled(RE::StaticFunctionTag*, bool enable) { g_obstacleBypassEnabled = enable; }
    static bool GetObstacleBypassEnabled(RE::StaticFunctionTag*) { return g_obstacleBypassEnabled; }

    static void SetAssistDuration(RE::StaticFunctionTag*, float ms) { if (ms >= 100.0f && ms <= 500.0f) g_assistDurationMs = ms; }
    static float GetAssistDuration(RE::StaticFunctionTag*) { return g_assistDurationMs; }
    static void SetKeepDistanceBoost(RE::StaticFunctionTag*, float val) { if (val >= 0.0f && val <= 0.3f) g_assistKeepDistanceBoost = val; }
    static float GetKeepDistanceBoost(RE::StaticFunctionTag*) { return g_assistKeepDistanceBoost; }
    static void SetFrictionReduction(RE::StaticFunctionTag*, float val) { if (val >= 0.0f && val <= 3.0f) g_assistFrictionMult = val; }
    static float GetFrictionReduction(RE::StaticFunctionTag*) { return g_assistFrictionMult; }
    static void SetMicroThreshold(RE::StaticFunctionTag*, float val) { if (val >= 1.0f && val <= 10.0f) g_assistMicroThreshold = val; }
    static float GetMicroThreshold(RE::StaticFunctionTag*) { return g_assistMicroThreshold; }

    static void SetCeilingMicroThreshold(RE::StaticFunctionTag*, float val) { if (val >= 1.0f && val <= 10.0f) g_ceilingMicroThreshold = val; }
    static float GetCeilingMicroThreshold(RE::StaticFunctionTag*) { return g_ceilingMicroThreshold; }
    static void SetLedgeMagnetMaxDepth(RE::StaticFunctionTag*, float val) { if (val >= 5.0f && val <= 30.0f) g_ledgeMagnetMaxDepth = val; }
    static float GetLedgeMagnetMaxDepth(RE::StaticFunctionTag*) { return g_ledgeMagnetMaxDepth; }
    static void SetLedgeMagnetStrength(RE::StaticFunctionTag*, float val) { if (val >= 0.0f && val <= 200.0f) g_ledgeMagnetStrength = val; }
    static float GetLedgeMagnetStrength(RE::StaticFunctionTag*) { return g_ledgeMagnetStrength; }
    static void SetVaultSnapEnabled(RE::StaticFunctionTag*, bool enable) { g_vaultSnapEnabled = enable; }
    static bool GetVaultSnapEnabled(RE::StaticFunctionTag*) { return g_vaultSnapEnabled; }
    static void SetVaultRequiredSpeed(RE::StaticFunctionTag*, float val) { if (val >= 100.0f && val <= 300.0f) g_vaultRequiredSpeed = val; }
    static float GetVaultRequiredSpeed(RE::StaticFunctionTag*) { return g_vaultRequiredSpeed; }

    static void SetNpcCollisionResponse(RE::StaticFunctionTag*, bool enable) { g_npcCollisionResponse = enable; }
    static bool GetNpcCollisionResponse(RE::StaticFunctionTag*) { return g_npcCollisionResponse; }
    static void SetNpcPushStrength(RE::StaticFunctionTag*, float val) { if (val >= 0.0f && val <= 150.0f) g_npcPushStrength = val; }
    static float GetNpcPushStrength(RE::StaticFunctionTag*) { return g_npcPushStrength; }
    static void SetNpcPushVertical(RE::StaticFunctionTag*, float val) { if (val >= 0.0f && val <= 50.0f) g_npcPushVertical = val; }
    static float GetNpcPushVertical(RE::StaticFunctionTag*) { return g_npcPushVertical; }
    static void SetPlayerRecoilStrength(RE::StaticFunctionTag*, float val) { if (val >= 0.0f && val <= 20.0f) g_playerRecoilStrength = val; }
    static float GetPlayerRecoilStrength(RE::StaticFunctionTag*) { return g_playerRecoilStrength; }
    static void SetCollisionRadius(RE::StaticFunctionTag*, float val) { if (val >= 50.0f && val <= 150.0f) g_collisionRadius = val; }
    static float GetCollisionRadius(RE::StaticFunctionTag*) { return g_collisionRadius; }

    static void SetCoyoteTimeWindow(RE::StaticFunctionTag*, float val) { if (val >= 0.0f && val <= 3.0f) g_coyoteTimeWindow = val; }
    static float GetCoyoteTimeWindow(RE::StaticFunctionTag*) { return g_coyoteTimeWindow; }

    static bool RegisterPapyrusFuncs(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("SetModEnabled", "DovaJump", SetModEnabled);
        vm->RegisterFunction("SetChargeTime", "DovaJump", SetChargeTime);
        vm->RegisterFunction("SetMaxMultiplier", "DovaJump", SetMaxMultiplier);
        vm->RegisterFunction("SetEnableNotifications", "DovaJump", SetEnableNotifications);
        vm->RegisterFunction("GetChargeTime", "DovaJump", GetChargeTime);
        vm->RegisterFunction("GetMaxMultiplier", "DovaJump", GetMaxMultiplier);
        vm->RegisterFunction("GetModEnabled", "DovaJump", GetModEnabled);
        vm->RegisterFunction("GetEnableNotifications", "DovaJump", GetEnableNotifications);

        vm->RegisterFunction("SetWallJumpEnabled", "DovaJump", SetWallJumpEnabled);
        vm->RegisterFunction("SetWallJumpHorizontalForce", "DovaJump", SetWallJumpHorizontalForce);
        vm->RegisterFunction("SetWallJumpVerticalForce", "DovaJump", SetWallJumpVerticalForce);
        vm->RegisterFunction("SetWallJumpAngleThreshold", "DovaJump", SetWallJumpAngleThreshold);
        vm->RegisterFunction("SetWallJumpCooldown", "DovaJump", SetWallJumpCooldown);
        vm->RegisterFunction("SetWallJumpReach", "DovaJump", SetWallJumpReach);
        vm->RegisterFunction("GetWallJumpHorizontalForce", "DovaJump", GetWallJumpHorizontalForce);
        vm->RegisterFunction("GetWallJumpVerticalForce", "DovaJump", GetWallJumpVerticalForce);
        vm->RegisterFunction("GetWallJumpAngleThreshold", "DovaJump", GetWallJumpAngleThreshold);
        vm->RegisterFunction("GetWallJumpCooldown", "DovaJump", GetWallJumpCooldown);
        vm->RegisterFunction("GetWallJumpReach", "DovaJump", GetWallJumpReach);
        vm->RegisterFunction("GetWallJumpEnabled", "DovaJump", GetWallJumpEnabled);

        vm->RegisterFunction("SetWASDMult_W", "DovaJump", SetWASDMult_W);
        vm->RegisterFunction("SetWASDMult_S", "DovaJump", SetWASDMult_S);
        vm->RegisterFunction("SetWASDMult_AD", "DovaJump", SetWASDMult_AD);
        vm->RegisterFunction("GetWASDMult_W", "DovaJump", GetWASDMult_W);
        vm->RegisterFunction("GetWASDMult_S", "DovaJump", GetWASDMult_S);
        vm->RegisterFunction("GetWASDMult_AD", "DovaJump", GetWASDMult_AD);
        vm->RegisterFunction("SetStaggerForce", "DovaJump", SetStaggerForce);
        vm->RegisterFunction("GetStaggerForce", "DovaJump", GetStaggerForce);

        vm->RegisterFunction("SetSlopeJumpEnabled", "DovaJump", SetSlopeJumpEnabled);
        vm->RegisterFunction("GetSlopeJumpEnabled", "DovaJump", GetSlopeJumpEnabled);

        vm->RegisterFunction("SetObstacleBypassEnabled", "DovaJump", SetObstacleBypassEnabled);
        vm->RegisterFunction("GetObstacleBypassEnabled", "DovaJump", GetObstacleBypassEnabled);

        vm->RegisterFunction("SetAssistDuration", "DovaJump", SetAssistDuration);
        vm->RegisterFunction("GetAssistDuration", "DovaJump", GetAssistDuration);
        vm->RegisterFunction("SetKeepDistanceBoost", "DovaJump", SetKeepDistanceBoost);
        vm->RegisterFunction("GetKeepDistanceBoost", "DovaJump", GetKeepDistanceBoost);
        vm->RegisterFunction("SetFrictionReduction", "DovaJump", SetFrictionReduction);
        vm->RegisterFunction("GetFrictionReduction", "DovaJump", GetFrictionReduction);
        vm->RegisterFunction("SetMicroThreshold", "DovaJump", SetMicroThreshold);
        vm->RegisterFunction("GetMicroThreshold", "DovaJump", GetMicroThreshold);

        vm->RegisterFunction("SetCeilingMicroThreshold", "DovaJump", SetCeilingMicroThreshold);
        vm->RegisterFunction("GetCeilingMicroThreshold", "DovaJump", GetCeilingMicroThreshold);
        vm->RegisterFunction("SetLedgeMagnetMaxDepth", "DovaJump", SetLedgeMagnetMaxDepth);
        vm->RegisterFunction("GetLedgeMagnetMaxDepth", "DovaJump", GetLedgeMagnetMaxDepth);
        vm->RegisterFunction("SetLedgeMagnetStrength", "DovaJump", SetLedgeMagnetStrength);
        vm->RegisterFunction("GetLedgeMagnetStrength", "DovaJump", GetLedgeMagnetStrength);
        vm->RegisterFunction("SetVaultSnapEnabled", "DovaJump", SetVaultSnapEnabled);
        vm->RegisterFunction("GetVaultSnapEnabled", "DovaJump", GetVaultSnapEnabled);
        vm->RegisterFunction("SetVaultRequiredSpeed", "DovaJump", SetVaultRequiredSpeed);
        vm->RegisterFunction("GetVaultRequiredSpeed", "DovaJump", GetVaultRequiredSpeed);

        vm->RegisterFunction("SetNpcCollisionResponse", "DovaJump", SetNpcCollisionResponse);
        vm->RegisterFunction("GetNpcCollisionResponse", "DovaJump", GetNpcCollisionResponse);
        vm->RegisterFunction("SetNpcPushStrength", "DovaJump", SetNpcPushStrength);
        vm->RegisterFunction("GetNpcPushStrength", "DovaJump", GetNpcPushStrength);
        vm->RegisterFunction("SetNpcPushVertical", "DovaJump", SetNpcPushVertical);
        vm->RegisterFunction("GetNpcPushVertical", "DovaJump", GetNpcPushVertical);
        vm->RegisterFunction("SetPlayerRecoilStrength", "DovaJump", SetPlayerRecoilStrength);
        vm->RegisterFunction("GetPlayerRecoilStrength", "DovaJump", GetPlayerRecoilStrength);
        vm->RegisterFunction("SetCollisionRadius", "DovaJump", SetCollisionRadius);
        vm->RegisterFunction("GetCollisionRadius", "DovaJump", GetCollisionRadius);

        vm->RegisterFunction("SetCoyoteTimeWindow", "DovaJump", SetCoyoteTimeWindow);
        vm->RegisterFunction("GetCoyoteTimeWindow", "DovaJump", GetCoyoteTimeWindow);

        logger::info("[Papyrus] All functions registered");
        return true;
    }

    //Jump Multiplier
    static float MultiplyJump() {
        if (!g_modEnabled) return g_originalMult();
        float base = g_originalMult();
        float mult = g_currentChargeMult.exchange(1.0f);
        if (mult <= 1.01f) return base;
        float result = base * mult;
        logger::info("[Jump] Charged: {:.2f}x, height={:.2f}", mult, result);
        if (g_enableNotifications) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Charged Jump: %.1fx height", mult);
            RE::DebugNotification(buf);
        }
        return result;
    }

    //CanProcess
    static bool HookedCanProcess(RE::JumpHandler* a_this, RE::InputEvent* a_event) {
        if (!g_modEnabled) return g_originalCanProcess(a_this, a_event);
        if (!a_event || a_event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton)
            return g_originalCanProcess(a_this, a_event);
        auto* button = static_cast<RE::ButtonEvent*>(a_event);
        auto* userEvents = RE::UserEvents::GetSingleton();
        if (userEvents && button->QUserEvent() == userEvents->jump) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && !player->IsDead()) {
                bool inWater = false;
                player->GetGraphVariableBool("bInWater", inWater);
                if (!inWater && !player->IsOnMount()) {
                    if (button->IsDown()) return false;
                }
            }
        }
        return g_originalCanProcess(a_this, a_event);
    }

    // Helper for getting an actor from a contact
    static RE::Actor* GetActorFromContact(const RE::hkpRootCdPoint& a_point, RE::Actor* a_player)
    {
        auto tryGetActor = [&](const RE::hkpCollidable* a_collidable) -> RE::Actor*
            {
                if (!a_collidable) return nullptr;
                auto* ref = RE::TESHavokUtilities::FindCollidableRef(*a_collidable);
                if (!ref || ref == a_player) return nullptr;
                return ref->As<RE::Actor>();
            };

        if (auto* actor = tryGetActor(a_point.rootCollidableA)) return actor;
        if (auto* actor = tryGetActor(a_point.rootCollidableB)) return actor;
        return nullptr;
    }

    // Функция для применения NPC толчка - Function for applying NPC push
    static void ApplyNpcCollision(RE::Actor* player, RE::Actor* target,
        const RE::NiPoint3& contactPos, const RE::NiPoint3& contactNormal)
    {
        if (!player || !target || target->IsDead() || target == player) return;

        static std::unordered_map<RE::FormID, steady_clock::time_point> cooldowns;
        auto now = steady_clock::now();
        auto it = cooldowns.find(target->GetFormID());
        if (it != cooldowns.end()) {
            if (duration<float>(now - it->second).count() < kNpcCollisionCooldown) {
                logger::info("[NpcCollision] Cooldown, skipped");
                return;
            }
        }
        cooldowns[target->GetFormID()] = now;

        RE::NiPoint3 playerPos = player->GetPosition();
        RE::NiPoint3 toNPC = contactPos - playerPos;
        float dist = toNPC.Length();
        if (dist < kNpcMinDistance) {
            logger::info("[NpcCollision] Too close, skipped");
            return;
        }
        toNPC /= dist;

        float dz = contactPos.z - (target->GetPosition().z + target->GetHeight() * 0.5f);
        float headThreshold = 120.0f;
        float torsoThreshold = 60.0f;
        float zoneMult = 1.0f;
        float verticalImpulse = g_npcPushVertical;

        if (dz > headThreshold) {
            zoneMult = 1.5f;
            verticalImpulse = -g_npcPushVertical * 0.5f;
        }
        else if (dz > torsoThreshold) {
            zoneMult = 1.0f;
        }
        else {
            zoneMult = 0.8f;
            verticalImpulse = g_npcPushVertical;
        }

        RE::NiPoint3 impulseDir = toNPC;
        impulseDir.z = 0.0f;
        impulseDir.Unitize();
        RE::NiPoint3 hitVelocity = impulseDir * (g_npcPushStrength * zoneMult);
        hitVelocity.z += verticalImpulse * zoneMult;

        auto* npcController = target->GetCharController();
        if (npcController) {
            RE::hkVector4 vel;
            npcController->GetLinearVelocityImpl(vel);
            vel.quad.m128_f32[0] += hitVelocity.x;
            vel.quad.m128_f32[1] += hitVelocity.y;
            vel.quad.m128_f32[2] += hitVelocity.z;
            npcController->SetLinearVelocityImpl(vel);
        }

        bool inStagger = false;
        target->GetGraphVariableBool("bInStagger", inStagger);
        if (!inStagger) {
            float staggerMag = std::clamp(g_npcPushStrength / 150.0f * zoneMult, 0.5f, 1.0f);
            target->NotifyAnimationGraph("staggerStop");
            target->SetGraphVariableFloat("staggerMagnitude", staggerMag);

            float angle = atan2(toNPC.x, toNPC.y) - target->GetAngleZ();
            target->SetGraphVariableFloat("staggerDirection", angle);

            target->NotifyAnimationGraph("staggerStart");
            logger::info("[NpcCollision] Directional stagger applied, angle={:.2f}", angle);
        }
        else {
            logger::info("[NpcCollision] NPC already in stagger, skipping animation");
        }

        if (g_playerRecoilStrength > 0.0f) {
            auto* playerCC = player->GetCharController();
            if (playerCC) {
                RE::hkVector4 vel;
                playerCC->GetLinearVelocityImpl(vel);
                vel.quad.m128_f32[0] -= toNPC.x * g_playerRecoilStrength;
                vel.quad.m128_f32[1] -= toNPC.y * g_playerRecoilStrength;
                vel.quad.m128_f32[2] += g_playerRecoilStrength * 0.1f;
                playerCC->SetLinearVelocityImpl(vel);
            }
        }
    }

    // NPC wall stagger (направленный стаггер без Skyphoria)
    static void ApplyNpcWallStagger(RE::Actor* target, const RE::NiPoint3& direction, float strength)
    {
        if (!target || target->IsDead()) return;

        bool inStagger = false;
        target->GetGraphVariableBool("bInStagger", inStagger);
        if (inStagger) {
            logger::info("[WallJump] NPC already in stagger, skipping");
            return;
        }

        float mag = std::clamp(strength / 100.0f, 0.5f, 1.0f);
        target->SetGraphVariableFloat("staggerMagnitude", mag);

        float angle = atan2(direction.x, direction.y) - target->GetAngleZ();
        target->SetGraphVariableFloat("staggerDirection", angle);

        target->NotifyAnimationGraph("staggerStart");

        auto* cc = target->GetCharController();
        if (cc) {
            RE::hkVector4 vel;
            cc->GetLinearVelocityImpl(vel);
            vel.quad.m128_f32[0] += direction.x * strength;
            vel.quad.m128_f32[1] += direction.y * strength;
            vel.quad.m128_f32[2] += direction.z * strength * 0.5f;
            cc->SetLinearVelocityImpl(vel);
        }

        logger::info("[WallJump] NPC wall stagger applied, dir=({:.2f},{:.2f},{:.2f}) strength={:.1f}",
            direction.x, direction.y, direction.z, strength);
    }

    // NPC collision processing
    static void ProcessNpcCollisions(RE::PlayerCharacter* player, RE::bhkCharacterController* cc)
    {
        if (!g_npcCollisionResponse) return;
        if (!player || !cc) return;
        if (!player->IsInMidair()) return;

        auto* pc = static_cast<RE::bhkCharProxyController*>(cc);
        auto* proxy = pc->GetCharacterProxy();
        if (!proxy) return;

        for (const RE::hkpRootCdPoint& point : proxy->manifold) {
            RE::Actor* target = GetActorFromContact(point, player);
            if (!target || target->IsDead()) continue;

            RE::NiPoint3 contactPos(
                point.contact.position.quad.m128_f32[0],
                point.contact.position.quad.m128_f32[1],
                point.contact.position.quad.m128_f32[2]);
            RE::NiPoint3 normal(
                point.contact.separatingNormal.quad.m128_f32[0],
                point.contact.separatingNormal.quad.m128_f32[1],
                point.contact.separatingNormal.quad.m128_f32[2]);

            ApplyNpcCollision(player, target, contactPos, normal);
        }
    }

    // Wall Jump Helper (с инерцией и поддержкой спринта для OAR - будущее)
    static bool TryWallJump(RE::PlayerCharacter* player, float holdTime, RE::ButtonEvent* buttonEvent) {
        logger::info("[WallJump] TryWallJump called, holdTime={:.3f}s", holdTime);

        if (!g_wallJumpEnabled) {
            logger::info("[WallJump] Wall jump disabled");
            return false;
        }

        if (g_hasWallJumped) {
            logger::info("[WallJump] Already wall jumped this airtime, skipping");
            return false;
        }

        auto now = steady_clock::now();
        float cooldownLeft = g_wallJumpCooldown - duration<float>(now - g_lastWallJump).count();
        if (cooldownLeft > 0.0f) {
            logger::info("[WallJump] Cooldown active, {:.3f}s left", cooldownLeft);
            return false;
        }

        auto* charController = player->GetCharController();
        if (!charController) {
            logger::info("[WallJump] No character controller");
            return false;
        }

        auto* proxyController = static_cast<RE::bhkCharProxyController*>(charController);
        auto* proxy = proxyController->GetCharacterProxy();
        if (!proxy) {
            logger::info("[WallJump] No character proxy");
            return false;
        }

        auto& manifold = proxy->manifold;
        logger::info("[WallJump] Manifold size = {}", manifold.size());

        RE::NiPoint3 contactNormal;
        RE::NiPoint3 contactPos;
        RE::Actor* contactedActor = nullptr;
        bool found = false;

        for (const RE::hkpRootCdPoint& point : manifold) {
            RE::NiPoint3 normal(
                point.contact.separatingNormal.quad.m128_f32[0],
                point.contact.separatingNormal.quad.m128_f32[1],
                point.contact.separatingNormal.quad.m128_f32[2]);

            if (std::abs(normal.z) < 0.95f) {
                contactNormal = normal;
                contactPos = RE::NiPoint3(
                    point.contact.position.quad.m128_f32[0],
                    point.contact.position.quad.m128_f32[1],
                    point.contact.position.quad.m128_f32[2]);
                contactedActor = GetActorFromContact(point, player);
                found = true;
                break;
            }
        }

        if (!found) {
            logger::info("[WallJump] No usable contact (only floor)");
            return false;
        }

        RE::NiPoint3 horizNormal = contactNormal;
        horizNormal.z = 0.0f;
        horizNormal.Unitize();

        logger::info("[WallJump] Contact normal: ({:.2f}, {:.2f}, {:.2f})", contactNormal.x, contactNormal.y, contactNormal.z);

        auto* camera = RE::PlayerCamera::GetSingleton();
        RE::NiPoint3 camForward(0.0f, 0.0f, 0.0f);
        RE::NiPoint3 camRight(1.0f, 0.0f, 0.0f);
        if (camera) {
            auto& camRot = camera->cameraRoot->world.rotate;
            camForward = RE::NiPoint3(camRot.entry[0][0], camRot.entry[1][0], 0.0f);
            camRight = RE::NiPoint3(camRot.entry[0][1], camRot.entry[1][1], 0.0f);
            camForward.Unitize();
            camRight.Unitize();
        }

        auto* controls = RE::PlayerControls::GetSingleton();
        RE::NiPoint2 moveInput = controls ? controls->data.moveInputVec : RE::NiPoint2{ 0.0f, 0.0f };

        RE::NiPoint3 jumpDir;
        float hForce = g_wallJumpHorizontalForce;
        float vForce = g_wallJumpVerticalForce;

        // Определяем, бежит ли игрок (зажат Shift) – читаем переменную графа
        bool isSprinting = false;
        player->GetGraphVariableBool("bIsSprinting", isSprinting);

        if (moveInput.y > 0.1f) {
            RE::NiPoint3 baseDir = -horizNormal;
            float alignment = camForward.Dot(baseDir);
            if (alignment > 0.3f) {
                baseDir = baseDir * 0.7f + camForward * 0.3f;
                baseDir.Unitize();
            }
            jumpDir = baseDir + RE::NiPoint3(0.0f, 0.0f, 0.7f);
            jumpDir.Unitize();
            hForce *= g_wMult;
            logger::info("[WallJump] Mode: W (mult={:.2f})", g_wMult);
        }
        else if (moveInput.y < -0.1f) {
            jumpDir = horizNormal;
            jumpDir.z = 0.3f;
            jumpDir.Unitize();
            hForce *= g_sMult;
            logger::info("[WallJump] Mode: S (mult={:.2f})", g_sMult);
        }
        else if (std::abs(moveInput.x) > 0.1f) {
            RE::NiPoint3 rawDir = camRight * (moveInput.x > 0.0f ? 1.0f : -1.0f);
            RE::NiPoint3 tangent = rawDir - horizNormal * rawDir.Dot(horizNormal);
            if (tangent.SqrLength() < 0.0001f) {
                rawDir = camForward;
                tangent = rawDir - horizNormal * rawDir.Dot(horizNormal);
            }
            tangent.Unitize();
            jumpDir = tangent + RE::NiPoint3(0.0f, 0.0f, 0.3f);
            jumpDir.Unitize();
            hForce *= g_adMult;
            logger::info("[WallJump] Mode: A/D (mult={:.2f})", g_adMult);
        }
        else {
            jumpDir = horizNormal * 0.2f;
            jumpDir.Unitize();
            logger::info("[WallJump] Mode: Idle");
        }

        // Сохранение инерции и расчёт новой скорости (без дополнительных множителей)
        RE::hkVector4 curVel;
        charController->GetLinearVelocityImpl(curVel);
        float oldX = curVel.quad.m128_f32[0];
        float oldY = curVel.quad.m128_f32[1];
        float oldZ = curVel.quad.m128_f32[2];

        float newX = jumpDir.x * hForce + oldX;
        float newY = jumpDir.y * hForce + oldY;
        float newZ = (oldZ < 0.0f ? 0.0f : oldZ * 0.3f) + (jumpDir.z * vForce);
        newZ = std::min(newZ, 600.0f);
        RE::hkVector4 newVel(newX, newY, newZ, 0.0f);
        charController->SetLinearVelocityImpl(newVel);

        //  Принудительный перезапуск анимации с явной установкой спринта
        player->SetGraphVariableBool("bInJumpState", false);
        player->SetGraphVariableBool("bInAir", false);
        player->SetGraphVariableFloat("fJumpHeight", 1.0f);

        // Устанавливаем флаг спринта ДО ProcessButton
        player->SetGraphVariableBool("bIsSprinting", isSprinting);

        auto* bcc = static_cast<RE::bhkCharacterController*>(charController);
        bcc->flags.set(RE::CHARACTER_FLAGS::kCanJump);
        bcc->flags.set(RE::CHARACTER_FLAGS::kJumping);
        bcc->context.currentState = RE::hkpCharacterStateType::kJumping;

        auto* pc = static_cast<RE::bhkCharProxyController*>(charController);
        if (auto* proxy_clean = pc->GetCharacterProxy()) {
            proxy_clean->manifold = {};
            proxy_clean->bodies = {};
            proxy_clean->phantoms = {};
        }

        // Вызов ProcessButton с переданным событием
        if (controls && buttonEvent) {
            float oldValue = buttonEvent->value;
            float oldHeld = buttonEvent->heldDownSecs;
            buttonEvent->value = 1.0f;
            buttonEvent->heldDownSecs = 0.0f;
            g_originalProcessButton(controls->jumpHandler, buttonEvent, &controls->data);
            buttonEvent->value = oldValue;
            buttonEvent->heldDownSecs = oldHeld;
            logger::info("[WallJump] Animation restart triggered via ProcessButton");
        }
        else {
            logger::warn("[WallJump] controls or buttonEvent is null, skipping ProcessButton");
        }

        // Повторно устанавливаем флаг спринта, так как ProcessButton мог его сбросить
        player->SetGraphVariableBool("bIsSprinting", isSprinting);

        // Запуск стандартного прыжка – OAR сам подхватит состояние спринта (bIsSprinting)
        player->NotifyAnimationGraph("jump");

        float h_max = (newZ * newZ) / (2.0f * kGravity);

        if (g_staggerForce > 0.0f && contactedActor) {
            RE::NiPoint3 staggerDir = -horizNormal;
            float strength = g_staggerForce * (hForce / g_wallJumpHorizontalForce);
            ApplyNpcWallStagger(contactedActor, staggerDir, strength);
        }

        g_hasWallJumped = true;
        g_lastWallJump = now;

        if (g_enableNotifications) RE::DebugNotification("Wall Jump!");

        logger::info("[WallJump] Jumped: dir=({:.2f},{:.2f},{:.2f}), H={:.1f}, V={:.1f}, Vz={:.1f}, h_max={:.1f}, sprint={}",
            jumpDir.x, jumpDir.y, jumpDir.z, hForce, vForce, newZ, h_max, isSprinting ? "yes" : "no");

        if (g_obstacleBypassEnabled) {
            g_assistStart = steady_clock::now();
            g_assistActive = true;
            g_assistParamsSaved = false;
        }

        return true;
    }

    //Хелпер состояния ванильного прыжка
    static bool GetVanillaJumpState(RE::PlayerCharacter* player,
        RE::hkpSurfaceInfo::SupportedState& outSupport,
        bool& outCanJump)
    {
        outSupport = RE::hkpSurfaceInfo::SupportedState::kUnsupported;
        outCanJump = false;

        auto* cc = player ? player->GetCharController() : nullptr;
        if (!cc) return false;

        auto* bcc = static_cast<RE::bhkCharacterController*>(cc);
        outSupport = bcc->surfaceInfo.supportedState.get();
        outCanJump = bcc->flags.any(RE::CHARACTER_FLAGS::kCanJump);
        return true;
    }

    // Slope Jump -
    static void DoSlopeJump(RE::PlayerCharacter* player, float elapsed, RE::PlayerControls* controls, RE::ButtonEvent* button)
    {
        auto* cc = player->GetCharController();
        if (!cc) return;

        auto* bcc = static_cast<RE::bhkCharacterController*>(cc);

        {
            auto* pc = static_cast<RE::bhkCharProxyController*>(cc);
            if (auto* proxy = pc->GetCharacterProxy()) {
                proxy->manifold = {};
                proxy->bodies = {};
                proxy->phantoms = {};
            }
        }

        RE::hkVector4 vel;
        cc->GetLinearVelocityImpl(vel);
        vel.quad.m128_f32[0] = 0.0f;
        vel.quad.m128_f32[1] = 0.0f;
        cc->SetLinearVelocityImpl(vel);

        bcc->flags.set(RE::CHARACTER_FLAGS::kCanJump);
        bcc->flags.set(RE::CHARACTER_FLAGS::kJumping);
        bcc->context.currentState = RE::hkpCharacterStateType::kJumping;

        float t = std::clamp(elapsed / g_chargeTime, 0.0f, 1.0f);
        float mult = 1.0f + (g_maxMultiplier - 1.0f) * t;
        g_currentChargeMult.store(mult);

        button->value = 1.0f;
        button->heldDownSecs = 0.0f;
        g_originalProcessButton(controls->jumpHandler, button, &controls->data);

        logger::info("[SlopeJump] State forced, ProcessButton called. mult={:.2f} (slope/stairs assist)", mult);

        if (g_obstacleBypassEnabled) {
            g_assistStart = steady_clock::now();
            g_assistActive = true;
            g_assistParamsSaved = false;
        }
    }

    // Обновление ассиста
    static void UpdateAssist()
    {
        if (!g_modEnabled) return;
        auto now = steady_clock::now();

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto* cc = player->GetCharController();
        if (!cc) return;

        ProcessNpcCollisions(player, cc);

        RE::hkpSurfaceInfo::SupportedState support;
        bool canJump;
        GetVanillaJumpState(player, support, canJump);

        // Обновляем время касания земли при ЛЮБОМ поддерживаемом состоянии,
        // чтобы окно coyote time работало и на склонах (где canJump может быть false)
        if (support == RE::hkpSurfaceInfo::SupportedState::kSupported) {
            if (steady_clock::now() < g_postJumpBlockUntil) return;   // защита от ложного coyote после прыжка
            g_lastGroundedTime = steady_clock::now();
            g_wasGrounded = true;
            g_hasWallJumped = false;

            auto* bcc = static_cast<RE::bhkCharacterController*>(cc);
            RE::NiPoint3 normal(
                bcc->surfaceInfo.surfaceNormal.quad.m128_f32[0],
                bcc->surfaceInfo.surfaceNormal.quad.m128_f32[1],
                bcc->surfaceInfo.surfaceNormal.quad.m128_f32[2]);
            g_wasOnSlope = (std::abs(normal.z) < 0.9f);
        }

        if (!g_assistActive || !g_obstacleBypassEnabled) return;

        auto* pc = static_cast<RE::bhkCharProxyController*>(cc);
        auto* proxy = pc->GetCharacterProxy();
        if (!proxy) return;

        float elapsed = duration<float, std::milli>(now - g_assistStart).count();
        if (elapsed >= g_assistDurationMs) {
            if (g_assistParamsSaved) {
                proxy->keepDistance = g_origKeepDistance;
                proxy->staticFriction = g_origStaticFriction;
                proxy->dynamicFriction = g_origDynamicFriction;
            }
            g_assistActive = false;
            g_assistParamsSaved = false;
            return;
        }

        if (!g_assistParamsSaved) {
            g_origKeepDistance = proxy->keepDistance;
            g_origStaticFriction = proxy->staticFriction;
            g_origDynamicFriction = proxy->dynamicFriction;
            g_assistParamsSaved = true;
        }

        proxy->keepDistance = g_origKeepDistance + g_assistKeepDistanceBoost;
        float mult = g_assistFrictionMult;
        proxy->staticFriction = g_origStaticFriction * mult;
        proxy->dynamicFriction = g_origDynamicFriction * mult;

        auto& m = proxy->manifold;
        int writeIdx = 0;
        for (int i = 0; i < static_cast<int>(m.size()); ++i) {
            const auto& pt = m[i];
            float depth = std::abs(pt.contact.separatingNormal.quad.m128_f32[3]);
            float nz = pt.contact.separatingNormal.quad.m128_f32[2];

            if (nz < -0.5f && depth < g_ceilingMicroThreshold) continue;

            if (nz < -0.5f && depth < g_ledgeMagnetMaxDepth) {
                RE::hkVector4 vel;
                cc->GetLinearVelocityImpl(vel);
                if (vel.quad.m128_f32[2] > 50.0f) {
                    RE::NiPoint3 normal(pt.contact.separatingNormal.quad.m128_f32[0],
                        pt.contact.separatingNormal.quad.m128_f32[1], 0.0f);
                    float len = std::sqrt(normal.x * normal.x + normal.y * normal.y);
                    if (len > 0.01f) {
                        normal.x /= len;
                        normal.y /= len;
                        vel.quad.m128_f32[0] += -normal.x * g_ledgeMagnetStrength;
                        vel.quad.m128_f32[1] += -normal.y * g_ledgeMagnetStrength;
                        cc->SetLinearVelocityImpl(vel);
                        logger::info("[Assist] Ledge Magnet applied");
                    }
                }
                continue;
            }

            if (g_vaultSnapEnabled && nz < -0.5f) {
                RE::hkVector4 vel;
                cc->GetLinearVelocityImpl(vel);
                if (vel.quad.m128_f32[2] >= g_vaultRequiredSpeed) {
                    RE::NiPoint3 normal(pt.contact.separatingNormal.quad.m128_f32[0],
                        pt.contact.separatingNormal.quad.m128_f32[1], 0.0f);
                    float len = std::sqrt(normal.x * normal.x + normal.y * normal.y);
                    if (len > 0.01f) {
                        normal.x /= len;
                        normal.y /= len;
                        vel.quad.m128_f32[0] = -normal.x * 200.0f;
                        vel.quad.m128_f32[1] = -normal.y * 200.0f;
                        vel.quad.m128_f32[2] = std::max(vel.quad.m128_f32[2], 200.0f);
                        cc->SetLinearVelocityImpl(vel);
                        logger::info("[Assist] Vault Snap applied");
                        proxy->manifold = {};
                        g_assistActive = false;
                        return;
                    }
                }
            }

            if (std::abs(depth) > g_assistMicroThreshold || std::abs(nz) > 0.5f) {
                m[writeIdx++] = m[i];
            }
        }
        *reinterpret_cast<int*>(&m._size) = writeIdx;
    }

    // Input Sink
    class JumpInputSink : public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static JumpInputSink* GetSingleton() {
            static JumpInputSink sink;
            return &sink;
        }

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
            RE::BSTEventSource<RE::InputEvent*>*) override
        {
            UpdateAssist();

            if (!g_modEnabled) return RE::BSEventNotifyControl::kContinue;

            if (!a_event || RE::UI::GetSingleton()->GameIsPaused())
                return RE::BSEventNotifyControl::kContinue;

            auto* event = *a_event;
            while (event) {
                if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
                    auto* button = static_cast<RE::ButtonEvent*>(event);
                    auto* userEvents = RE::UserEvents::GetSingleton();
                    if (userEvents && button->QUserEvent() == userEvents->jump) {
                        if (button->IsDown()) {
                            g_holdStart = steady_clock::now();
                            g_isHolding.store(true);
                        }
                        else if (button->IsUp() && g_isHolding.load()) {
                            g_isHolding.store(false);
                            float elapsed = duration<float>(steady_clock::now() - g_holdStart).count();

                            auto* player = RE::PlayerCharacter::GetSingleton();
                            auto* controls = RE::PlayerControls::GetSingleton();
                            if (player && controls && !player->IsDead()) {
                                bool inWater = false;
                                player->GetGraphVariableBool("bInWater", inWater);
                                if (!inWater && !player->IsOnMount()) {

                                    RE::hkpSurfaceInfo::SupportedState support{};
                                    bool canJump = false;
                                    GetVanillaJumpState(player, support, canJump);

                                    // Определяем, на склоне ли мы прямо сейчас
                                    bool slopeNow = false;
                                    if (support == RE::hkpSurfaceInfo::SupportedState::kSupported) {
                                        auto* bcc = static_cast<RE::bhkCharacterController*>(player->GetCharController());
                                        RE::NiPoint3 normal(
                                            bcc->surfaceInfo.surfaceNormal.quad.m128_f32[0],
                                            bcc->surfaceInfo.surfaceNormal.quad.m128_f32[1],
                                            bcc->surfaceInfo.surfaceNormal.quad.m128_f32[2]);
                                        slopeNow = (std::abs(normal.z) < 0.9f);
                                    }

                                    if (g_verboseLogging) {
                                        logger::info("[Input] Jump attempt: modEnabled=1, inWater=0, onMount=0,"
                                            " isInMidair={}, supportState={}, canJump={}, slopeNow={}",
                                            player->IsInMidair() ? 1 : 0,
                                            static_cast<int>(support), canJump ? 1 : 0, slopeNow ? 1 : 0);
                                    }

                                    if (support == RE::hkpSurfaceInfo::SupportedState::kSupported && canJump) {
                                        if (g_slopeJumpEnabled && slopeNow) {
                                            logger::info("[Input]   -> On slope/stairs, forcing slope jump");
                                            DoSlopeJump(player, elapsed, controls, button);
                                            g_wasGrounded = false;
                                            g_lastGroundedTime = steady_clock::time_point();
                                            g_postJumpBlockUntil = steady_clock::now() + milliseconds(100);
                                        }
                                        else {
                                            float t = std::clamp(elapsed / g_chargeTime, 0.0f, 1.0f);
                                            g_currentChargeMult.store(1.0f + (g_maxMultiplier - 1.0f) * t);
                                            logger::info("[Input] Vanilla ground jump: hold={:.3f}s, mult={:.2f}x",
                                                elapsed, g_currentChargeMult.load());
                                            button->value = 1.0f;
                                            button->heldDownSecs = 0.0f;
                                            g_originalProcessButton(controls->jumpHandler, button, &controls->data);
                                            g_wasGrounded = false;
                                            g_lastGroundedTime = steady_clock::time_point();
                                            g_postJumpBlockUntil = steady_clock::now() + milliseconds(100);
                                            if (g_obstacleBypassEnabled) {
                                                g_assistStart = steady_clock::now();
                                                g_assistActive = true;
                                                g_assistParamsSaved = false;
                                            }
                                        }
                                    }
                                    else if ((support == RE::hkpSurfaceInfo::SupportedState::kSupported && !canJump) ||
                                        support == RE::hkpSurfaceInfo::SupportedState::kSliding) {
                                        if (g_slopeJumpEnabled) {
                                            logger::info("[Input]   -> Slope/Sliding state, forcing jump state");
                                            DoSlopeJump(player, elapsed, controls, button);
                                            g_wasGrounded = false;
                                            g_lastGroundedTime = steady_clock::time_point();
                                            g_postJumpBlockUntil = steady_clock::now() + milliseconds(100);
                                        }
                                        else {
                                            logger::info("[Input]   -> Slope/Sliding state, but Slope Jump disabled");
                                        }
                                    }
                                    else {
                                        // check coyote time before wall jump
                                        if (g_wasGrounded && (steady_clock::now() - g_lastGroundedTime) < duration<float>(g_coyoteTimeWindow)) {
                                            float t = std::clamp(elapsed / g_chargeTime, 0.0f, 1.0f);
                                            g_currentChargeMult.store(1.0f + (g_maxMultiplier - 1.0f) * t);
                                            logger::info("[Input] Coyote jump: hold={:.3f}s, mult={:.2f}x",
                                                elapsed, g_currentChargeMult.load());
                                            button->value = 1.0f;
                                            button->heldDownSecs = 0.0f;
                                            if (g_wasOnSlope || slopeNow) {
                                                DoSlopeJump(player, elapsed, controls, button);
                                            }
                                            else {
                                                // Jump forcedly allowed during coyote time in mid-air.
                                                auto* cc = player->GetCharController();
                                                auto* bcc = static_cast<RE::bhkCharacterController*>(cc);
                                                bcc->flags.set(RE::CHARACTER_FLAGS::kCanJump);
                                                bcc->flags.set(RE::CHARACTER_FLAGS::kJumping);
                                                bcc->context.currentState = RE::hkpCharacterStateType::kJumping;

                                                g_originalProcessButton(controls->jumpHandler, button, &controls->data);
                                            }
                                            g_wasGrounded = false;
                                            // Время не сбрасываем, чтобы не поломать окно при честном сходе
                                            // (g_wasGrounded уже false, следующее обновление времени будет только при касании земли)

                                            if (g_obstacleBypassEnabled) {
                                                g_assistStart = steady_clock::now();
                                                g_assistActive = true;
                                                g_assistParamsSaved = false;
                                            }
                                        }
                                        else {
                                            logger::info("[Input]   -> Unsupported, attempting wall jump");
                                            logger::info("[Input] In air, attempting wall jump (hold={:.3f}s)", elapsed);
                                            if (!TryWallJump(player, elapsed, button)) {
                                                logger::info("[Input] Wall jump failed");
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                event = event->next;
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // installation
    static void InstallHooks() {
        logger::info("[Init] DovaJump v0.9.1.3 ready (wall jump with sprint animation)");

        // Настройки больше не сбрасываются здесь
        // Их дефолты заданы при статической инициализации, а реальные значения
        // загружаются из сейва через LoadSettings.

        auto* controls = RE::PlayerControls::GetSingleton();
        if (!controls) return;

        auto* handler = controls->jumpHandler;
        if (!handler) return;

        g_handler = handler;
        void** oldVtable = *reinterpret_cast<void***>(handler);
        g_oldVtable = oldVtable;
        if (!oldVtable) return;

        size_t count = 0;
        while (oldVtable[count] != nullptr) count++;
        g_vtableSize = count;

        g_clonedVtable = new void* [g_vtableSize];
        std::memcpy(g_clonedVtable, oldVtable, sizeof(void*) * g_vtableSize);

        uintptr_t origCanProcessAddr = reinterpret_cast<uintptr_t>(oldVtable[1]);
        std::memcpy(&g_originalCanProcess, &origCanProcessAddr, sizeof(origCanProcessAddr));

        uintptr_t origProcessButtonAddr = reinterpret_cast<uintptr_t>(oldVtable[4]);
        std::memcpy(&g_originalProcessButton, &origProcessButtonAddr, sizeof(origProcessButtonAddr));

        uintptr_t hookCanProcessAddr = reinterpret_cast<uintptr_t>(&HookedCanProcess);
        std::memcpy(&g_clonedVtable[1], &hookCanProcessAddr, sizeof(hookCanProcessAddr));

        *reinterpret_cast<void***>(handler) = g_clonedVtable;

        auto* inputMgr = RE::BSInputDeviceManager::GetSingleton();
        if (inputMgr) inputMgr->AddEventSink(JumpInputSink::GetSingleton());

        multCallSite = REL::ID(37257).address() + 0x17F;
        uintptr_t origMult = ReadDisp(multCallSite);
        g_originalMult = reinterpret_cast<JumpMultFunc_t>(origMult);
        auto& trampoline = SKSE::GetTrampoline();
        trampoline.write_call<5>(multCallSite, reinterpret_cast<uintptr_t>(&MultiplyJump));
        g_trampolineActive = true;

        if (g_enableNotifications) RE::DebugNotification("DovaJump v0.9.1.3 ready");
    }

    static void OnSKSEMessage(SKSE::MessagingInterface::Message* msg) {
        if (msg && msg->type == SKSE::MessagingInterface::kDataLoaded) {
            InstallHooks();
        }
    }

    extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
        SetupLog();
        SKSE::Init(skse);
        SKSE::AllocTrampoline(128);

        if (auto* messaging = SKSE::GetMessagingInterface())
            messaging->RegisterListener(OnSKSEMessage);
        else return false;

        if (auto* papyrus = SKSE::GetPapyrusInterface())
            papyrus->Register(RegisterPapyrusFuncs);

        if (auto* serde = SKSE::GetSerializationInterface()) {
            serde->SetUniqueID('DOJ_');
            serde->SetSaveCallback([](SKSE::SerializationInterface* s) { SaveSettings(s); });
            serde->SetLoadCallback([](SKSE::SerializationInterface* s) { LoadSettings(s); });
        }

        return true;
    }
}
