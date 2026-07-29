// Skyphoria.cpp – v4.2-Diag3 (forced target foot, no height control, ik debug)
/*A test build for Skyphoria development. Just the beginning. It doesn't work yet—it's merely a rough draft.
It's hosted here simply so I don't lose it. 
There is no DovaJump integration (it was planned for jumps off NPCs, but that's still a work in progress).*/
#define SKYPHORIA_VERSION "4.2-Diag3"

#include "Skyphoria.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <xmmintrin.h>
#include <string_view>
#include <vector>

#include "SKSE/SKSE.h"
#include "RE/B/BShkbAnimationGraph.h"
#include "RE/H/hkbRagdollDriver.h"
#include "RE/H/hkaRagdollInstance.h"
#include "RE/H/hkpRagdollConstraintData.h"
#include "RE/H/hkpConstraintMotor.h"
#include "RE/H/hkpRigidBody.h"
#include "RE/H/hkpMotion.h"
#include "RE/H/hkpConstraintAtom.h"
#include "RE/H/hkpConstraintData.h"
#include "RE/H/hkpWorld.h"
#include "RE/B/bhkWorld.h"
#include "RE/B/bhkNiCollisionObject.h"
#include "RE/B/bhkCharacterController.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiAVObject.h"
#include "RE/T/TES.h"
#include "RE/H/hkpContactListener.h"
#include "RE/H/hkpContactPointEvent.h"
#include "RE/H/hkContactPoint.h"
#include "RE/H/hkArray.h"
#include "RE/H/hkpWorldRayCastInput.h"
#include "RE/H/hkpWorldRayCastOutput.h"
#include "RE/H/hkQsTransform.h"
#include "RE/H/hkpCollidable.h"
#include "RE/H/hkQuaternion.h"

#include "logger.h"

namespace Skyphoria
{
    std::unordered_map<RE::ActorHandle, EuphoriaState, ActorHandleHash> g_activeStates;
    std::mutex g_stateMutex;
    Settings   g_settings;

    static GlobalContactListener g_contactListener;
    static bool g_contactListenerRegistered = false;

    // Converters
    static float HavokToGame(float h) { return h * RE::bhkWorld::GetWorldScaleInverse(); }
    static float GameToHavok(float g) { return g / RE::bhkWorld::GetWorldScaleInverse(); }
    static RE::NiPoint3 HavokToGame(const RE::NiPoint3& havok) { return havok * RE::bhkWorld::GetWorldScaleInverse(); }
    static RE::NiPoint3 GameToHavok(const RE::NiPoint3& game) { return game / RE::bhkWorld::GetWorldScaleInverse(); }

    // Auxiliary functions
    static RE::Actor* LookupActor(RE::ActorHandle h) { auto p = h.get(); return p.get(); }
    static RE::BShkbAnimationGraph* GetAnimationGraph(RE::Actor* a) {
        if (!a) return nullptr;
        RE::BSTSmartPointer<RE::BSAnimationGraphManager> mgr;
        if (!a->GetAnimationGraphManager(mgr) || !mgr || mgr->graphs.empty()) return nullptr;
        return mgr->graphs[0].get();
    }
    static void ModifyCharacterController(RE::Actor* actor, bool enter, uint32_t& saved, bool& savedB) {
        auto* cc = actor->GetCharController(); auto* bcc = cc ? static_cast<RE::bhkCharacterController*>(cc) : nullptr;
        if (!bcc) return;
        if (enter) { saved = bcc->flags.underlying(); savedB = true; bcc->flags.set(RE::CHARACTER_FLAGS::kFollowRagdoll); }
        else { if (savedB) { bcc->flags.reset(RE::CHARACTER_FLAGS::kFollowRagdoll); bcc->flags = static_cast<RE::CHARACTER_FLAGS>(saved); savedB = false; } }
    }
    static float GetGroundHeight(const RE::NiPoint3& pos) {
        auto* tes = RE::TES::GetSingleton();
        if (!tes) return pos.z;
        float landHeight = pos.z;
        if (tes->GetLandHeight(pos, landHeight)) return landHeight;
        return pos.z;
    }

    //Procedural Control
    static RE::NiPoint3 GetBoneWorldPosition(RE::Actor* actor, const char* boneName) {
        auto* root = actor->Get3D(false);
        if (!root) return {};
        auto* node = root->GetObjectByName(boneName);
        return node ? node->world.translate : RE::NiPoint3{};
    }
    static RE::NiMatrix3 GetBoneLocalRotation(RE::Actor* actor, const char* boneName) {
        auto* root = actor->Get3D(false);
        if (!root) return {};
        auto* node = root->GetObjectByName(boneName);
        return node ? node->local.rotate : RE::NiMatrix3{};
    }
    static void FreezeAnimation(RE::Actor* actor) {
        if (auto* g = GetAnimationGraph(actor)) g->ToggleSyncOnUpdate(false);
        actor->SetGraphVariableBool("bInStagger", true); // AI off
    }
    static void UnfreezeAnimation(RE::Actor* actor) {
        actor->SetGraphVariableBool("bInStagger", false);
        if (auto* g = GetAnimationGraph(actor)) g->ToggleSyncOnUpdate(true);
    }
    static void UpdateFullHierarchy(RE::Actor* actor) {
        if (auto* root = actor->Get3D(false)) {
            RE::NiUpdateData data{ 0.0f, RE::NiUpdateData::Flag::kDirty };
            root->UpdateWorldData(&data);
        }
    }

    // IK: returns local Euler angles (X-lift, Z-rotation)
    static bool SimpleIKLegLocal(const RE::NiPoint3& hipWorld, const RE::NiPoint3& targetWorld,
        float thigh, float calf,
        RE::NiPoint3& hipEuler, RE::NiPoint3& kneeEuler) {
        RE::NiPoint3 to = targetWorld - hipWorld;
        float dist = to.Length();
        float maxReach = thigh + calf;
        if (dist > maxReach) { to *= maxReach / dist; dist = maxReach; }
        float cosKnee = (thigh * thigh + calf * calf - dist * dist) / (2.0f * thigh * calf);
        cosKnee = std::clamp(cosKnee, -1.0f, 1.0f);
        float kneeAngle = std::acos(cosKnee);
        float lift = std::atan2(to.z, std::sqrt(to.x * to.x + to.y * to.y));
        float swing = std::atan2(to.x, to.y);
        float offset = std::asin((calf * std::sin(kneeAngle)) / dist);
        hipEuler = { lift + offset, 0.0f, swing };
        kneeEuler = { -kneeAngle, 0.0f, 0.0f };
        return true;
    }

    static void ApplyLocalRotation(RE::Actor* actor, const char* boneName, const RE::NiPoint3& euler) {
        auto* root = actor->Get3D(false); if (!root) return;
        auto* node = root->GetObjectByName(boneName); if (!node) return;
        float cx = std::cos(euler.x), sx = std::sin(euler.x);
        float cy = std::cos(euler.y), sy = std::sin(euler.y);
        float cz = std::cos(euler.z), sz = std::sin(euler.z);
        RE::NiMatrix3 rot;
        rot.entry[0][0] = cy * cz;          rot.entry[0][1] = cy * sz;          rot.entry[0][2] = -sy;
        rot.entry[1][0] = sx * sy * cz - cx * sz; rot.entry[1][1] = sx * sy * sz + cx * cz; rot.entry[1][2] = sx * cy;
        rot.entry[2][0] = cx * sy * cz + sx * sz; rot.entry[2][1] = cx * sy * sz - sx * cz; rot.entry[2][2] = cx * cy;
        node->local.rotate = rot;
    }

    // Global
    void GlobalContactListener::ContactPointCallback(const RE::hkpContactPointEvent&) {}

    void Init() {
        std::lock_guard lock(g_stateMutex);
        g_activeStates.clear();
        if (!g_contactListenerRegistered) {
            if (auto* player = RE::PlayerCharacter::GetSingleton())
                if (auto* cell = player->GetParentCell())
                    if (auto* bhkWorld = cell->GetbhkWorld())
                        if (auto* world = bhkWorld->GetWorld1()) {
                            world->contactListeners.push_back(&g_contactListener);
                            g_contactListenerRegistered = true;
                        }
        }
        g_settings.verboseLogging = true;
        logger::info("[Skyphoria] Initialized version {}", SKYPHORIA_VERSION);
    }

    void ApplyEuphoriaStagger(RE::Actor* target, const RE::NiPoint3& impulse) {
        if (!target || target->IsDead()) return;
        RE::ActorHandle handle = target->GetHandle();
        logger::info("[Skyphoria] ApplyEuphoriaStagger {:08X} v{}", target->GetFormID(), SKYPHORIA_VERSION);

        {
            std::lock_guard lock(g_stateMutex);
            if (auto it = g_activeStates.find(handle); it != g_activeStates.end()) {
                auto* actor = LookupActor(it->second.actorHandle);
                if (actor) {
                    UnfreezeAnimation(actor);
                    ModifyCharacterController(actor, false, it->second.savedCCFlags, it->second.ccFlagsSaved);
                }
                g_activeStates.erase(it);
            }
        }

        FreezeAnimation(target);

        EuphoriaState s;
        s.elapsed = 0.0f;
        s.actorHandle = handle;
        s.initialImpulse = impulse;
        s.phase = Phase::Impact;
        s.startupTimer = 0.5f;

        RE::NiPoint3 hip = GetBoneWorldPosition(target, "NPC L Thigh [LThg]");
        RE::NiPoint3 knee = GetBoneWorldPosition(target, "NPC L Calf [LClf]");
        RE::NiPoint3 foot = GetBoneWorldPosition(target, "NPC L Foot [Lft ]");
        s.thighLen = (knee - hip).Length();
        s.calfLen = (foot - knee).Length();

        uint32_t savedFlags = 0; bool flagsSaved = false;
        ModifyCharacterController(target, true, savedFlags, flagsSaved);
        s.savedCCFlags = savedFlags; s.ccFlagsSaved = flagsSaved;

        {
            std::lock_guard lock(g_stateMutex);
            g_activeStates[handle] = s;
        }
        logger::info("[Skyphoria] Balance state stored for {:08X}", target->GetFormID());
    }

    void Update(float dt) {
        std::lock_guard lock(g_stateMutex);
        for (auto it = g_activeStates.begin(); it != g_activeStates.end(); ) {
            auto& s = it->second;
            s.elapsed += dt;
            s.startupTimer -= dt;

            auto* actor = LookupActor(s.actorHandle);
            if (!actor || actor->IsDead()) {
                if (actor) { UnfreezeAnimation(actor); ModifyCharacterController(actor, false, s.savedCCFlags, s.ccFlagsSaved); }
                it = g_activeStates.erase(it); continue;
            }

            // We forcibly set a target point for the left foot.
            RE::NiPoint3 hipPos = GetBoneWorldPosition(actor, "NPC L Thigh [LThg]");
            RE::NiPoint3 footPos = GetBoneWorldPosition(actor, "NPC L Foot [Lft ]");
            RE::NiPoint3 targetFoot = footPos + RE::NiPoint3(0.0f, 30.0f, 20.0f); // вперёд (Y) и вверх (Z)

            // Logging altitudes.
            float groundHip = GetGroundHeight(hipPos);
            float groundFoot = GetGroundHeight(footPos);
            logger::info("[Skyphoria] {:08X} t={:.2f} hipZ={:.1f} footZ={:.1f} gndHip={:.1f} gndFoot={:.1f}",
                actor->GetFormID(), s.elapsed, hipPos.z, footPos.z, groundHip, groundFoot);

            RE::NiPoint3 hipEuler, kneeEuler;
            if (SimpleIKLegLocal(hipPos, targetFoot, s.thighLen, s.calfLen, hipEuler, kneeEuler)) {
                logger::info("[Skyphoria] {:08X} IK hip=({:.2f},{:.2f},{:.2f}) knee=({:.2f},{:.2f},{:.2f})",
                    actor->GetFormID(), hipEuler.x, hipEuler.y, hipEuler.z, kneeEuler.x, kneeEuler.y, kneeEuler.z);
                ApplyLocalRotation(actor, "NPC L Thigh [LThg]", hipEuler);
                ApplyLocalRotation(actor, "NPC L Calf [LClf]", kneeEuler);
                UpdateFullHierarchy(actor);

                RE::NiPoint3 newFoot = GetBoneWorldPosition(actor, "NPC L Foot [Lft ]");
                logger::info("[Skyphoria] {:08X} new foot=({:.1f},{:.1f},{:.1f})", actor->GetFormID(), newFoot.x, newFoot.y, newFoot.z);
            }

            ++it;
        }
    }
}