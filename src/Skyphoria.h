#pragma once
// Skyphoria.h (v4.2)
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "RE/Skyrim.h"
#include "RE/H/hkpContactListener.h"
#include "RE/N/NiMatrix3.h"
#include "RE/N/NiTransform.h"

struct ActorHandleHash
{
    size_t operator()(const RE::ActorHandle &h) const noexcept
    {
        return std::hash<uint32_t>{}(h.native_handle());
    }
};

namespace Skyphoria
{
    enum class Phase
    {
        Impact,
        AnkleHip,
        Swing,
        DoubleSupport,
        PostStable
    };

    struct Settings
    {
        float impactImpulseMult = 5.0f;
        float gravity = 600.0f;
        float footLength = 30.0f, footWidth = 15.0f;
        float stepTriggerMargin = 5.0f, minSpeedForMargin = 1.0f;
        float stableLinearVelThreshold = 15.0f, stableHeightTolerance = 0.25f;
        int stableFramesRequired = 30;
        float fallHeightThreshold = 25.0f;
        float stepDuration = 0.6f, stepHeight = 25.0f, maxStepLength = 45.0f, stepCooldown = 0.1f;
        float footContactFilterTau = 0.05f;
        int footContactRequiredFrames = 5;
        float standingPelvisHeight = 120.0f;
        float startupGracePeriod = 0.5f;
        bool verboseLogging = false;
    };

    struct EuphoriaState
    {
        float elapsed = 0.0f;
        RE::ActorHandle actorHandle;
        RE::NiPoint3 initialImpulse;
        Phase phase = Phase::Impact;
        float startupTimer = 0.5f;
        bool lFootHitThisFrame = false, rFootHitThisFrame = false;
        bool lFootGrounded = false, rFootGrounded = false;
        int lFootContactFrames = 0, rFootContactFrames = 0;
        float thighLen = 50.0f, calfLen = 50.0f;
        RE::NiPoint3 prevPelvis;
        RE::NiPoint3 filtLFoot, filtRFoot;
        float stableTimer = 0.0f;
        bool hasFallen = false;
        RE::NiMatrix3 hipLocalOrig, kneeLocalOrig, hipLocalOrigR, kneeLocalOrigR;
        uint32_t savedCCFlags = 0;
        bool ccFlagsSaved = false;
        float swingTimer = 0.0f, swingDuration = 0.6f;
        bool swingFootIsLeft = false;
        float stepLength = 0.0f;
        RE::NiPoint3 swingStartPos, swingTargetPos, swingDirection;
        float stepCooldownTimer = 0.0f;
        int stepCount = 0;
        float leftFootRatio = 0.5f;
        float blendTimer = 0.0f;
    };

    extern std::unordered_map<RE::ActorHandle, EuphoriaState, ActorHandleHash> g_activeStates;
    extern std::mutex g_stateMutex;
    extern Settings g_settings;

    class GlobalContactListener : public RE::hkpContactListener
    {
    public:
        void ContactPointCallback(const RE::hkpContactPointEvent &event) override;
    };

    void Init();
    void ApplyEuphoriaStagger(RE::Actor *target, const RE::NiPoint3 &impulse);
    void Update(float deltaTime);
}