#pragma once

#include "RE/Skyrim.h"

namespace JumpRequirements
{
    struct Requirement
    {
        float level{0.0f};

        std::string perkPlugin;
        RE::FormID perkFormID{0};

        RE::BGSPerk *perk{nullptr};
    };

    void Load();

    bool MeetsChargeJumpRequirement(RE::Actor *actor);
    bool MeetsWallJumpRequirement(RE::Actor *actor);
    bool MeetsEnemyStaggerJumpRequirement(RE::Actor *actor);
    bool MeetsDoubleJumpRequirement(RE::Actor *actor);

    const Requirement &GetChargeJumpRequirement();
    const Requirement &GetWallJumpRequirement();
    const Requirement &GetEnemyStaggerJumpRequirement();
    const Requirement &GetDoubleJumpRequirement();

    float GetWallJumpStaggerCooldown();
    int GetMaxAirJumps();
    float GetMinStamina();
}