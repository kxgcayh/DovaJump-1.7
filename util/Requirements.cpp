#include "Requirements.h"

#include <SKSE/SKSE.h>

#include "logger.h"

#include <SimpleIni.h>

namespace JumpRequirements
{
    namespace
    {
        Requirement g_chargeJumpRequirement{};
        Requirement g_wallJumpRequirement{};
        Requirement g_enemyStaggerJumpRequirement{};
        Requirement g_doubleJumpRequirement{};

        float g_wallJumpStaggerCooldown{5.0f};
        float g_minStamina{0.0f};

        int g_maxAirJumps{1};

        bool g_debugLogging{false};
        RE::ActorValue g_skillType{RE::ActorValue::kSneak};

        RE::ActorValue ParseSkillType(const char *value)
        {
            if (!value || !*value)
            {
                return RE::ActorValue::kSneak;
            }

            const std::string skill = value;

            if (skill == "OneHanded")
                return RE::ActorValue::kOneHanded;
            if (skill == "TwoHanded")
                return RE::ActorValue::kTwoHanded;
            if (skill == "Archery")
                return RE::ActorValue::kArchery;
            if (skill == "Block")
                return RE::ActorValue::kBlock;
            if (skill == "Smithing")
                return RE::ActorValue::kSmithing;
            if (skill == "HeavyArmor")
                return RE::ActorValue::kHeavyArmor;
            if (skill == "LightArmor")
                return RE::ActorValue::kLightArmor;
            if (skill == "Pickpocket")
                return RE::ActorValue::kPickpocket;
            if (skill == "Lockpicking")
                return RE::ActorValue::kLockpicking;
            if (skill == "Sneak")
                return RE::ActorValue::kSneak;
            if (skill == "Alchemy")
                return RE::ActorValue::kAlchemy;
            if (skill == "Speech")
                return RE::ActorValue::kSpeech;
            if (skill == "Alteration")
                return RE::ActorValue::kAlteration;
            if (skill == "Conjuration")
                return RE::ActorValue::kConjuration;
            if (skill == "Destruction")
                return RE::ActorValue::kDestruction;
            if (skill == "Illusion")
                return RE::ActorValue::kIllusion;
            if (skill == "Restoration")
                return RE::ActorValue::kRestoration;
            if (skill == "Enchanting")
                return RE::ActorValue::kEnchanting;

            logger::warn(
                "JumpRequirements: Unknown SkillType '{}', defaulting to Sneak",
                skill);

            return RE::ActorValue::kSneak;
        }

        void LoadRequirement(
            CSimpleIniA &ini,
            const char *section,
            Requirement &requirement)
        {
            const double level =
                ini.GetDoubleValue(section, "Level", 0.0);

            requirement.level =
                static_cast<float>(level);

            const char *perkPlugin =
                ini.GetValue(section, "PerkPlugin", "");

            requirement.perkPlugin =
                perkPlugin ? perkPlugin : "";

            const char *perkFormID =
                ini.GetValue(section, "PerkFormID", "");

            requirement.perkFormID = 0;
            requirement.perk = nullptr;

            if (perkFormID && *perkFormID)
            {
                try
                {
                    requirement.perkFormID =
                        static_cast<RE::FormID>(
                            std::stoul(perkFormID, nullptr, 0));
                }
                catch (...)
                {
                    logger::error(
                        "JumpRequirements: Invalid PerkFormID '{}' in [{}]",
                        perkFormID,
                        section);

                    requirement.perkFormID = 0;
                }
            }

            logger::info(
                "JumpRequirements: [{}] Level={:.1f} | Plugin='{}' | FormID={:08X}",
                section,
                requirement.level,
                requirement.perkPlugin,
                requirement.perkFormID);
        }

        void ResolveRequirementPerk(
            const char *section,
            Requirement &requirement)
        {
            requirement.perk = nullptr;

            if (requirement.perkPlugin.empty() ||
                requirement.perkFormID == 0)
            {
                logger::info(
                    "JumpRequirements: [{}] No perk configured -> using skill level",
                    section);

                return;
            }

            auto *dataHandler =
                RE::TESDataHandler::GetSingleton();

            if (!dataHandler)
            {
                logger::error(
                    "JumpRequirements: [{}] TESDataHandler unavailable",
                    section);

                return;
            }

            auto *form =
                dataHandler->LookupForm(
                    requirement.perkFormID,
                    requirement.perkPlugin);

            if (!form)
            {
                logger::error(
                    "JumpRequirements: [{}] LookupForm FAILED | {} | {:08X}",
                    section,
                    requirement.perkPlugin,
                    requirement.perkFormID);

                return;
            }

            requirement.perk =
                skyrim_cast<RE::BGSPerk *>(form);

            if (!requirement.perk)
            {
                logger::error(
                    "JumpRequirements: [{}] Form is NOT a BGSPerk",
                    section);

                return;
            }

            logger::info(
                "JumpRequirements: [{}] Perk resolved | {:08X}",
                section,
                requirement.perk->GetFormID());
        }

        bool MeetsRequirementInternal(
            RE::Actor *actor,
            const char *section,
            const Requirement &requirement)
        {
            if (!actor)
            {
                logger::error(
                    "JumpRequirements: [{}] NULL actor",
                    section);

                return false;
            }

            //
            // Global minimum stamina requirement
            //
            if (g_minStamina > 0.0f)
            {
                auto *avOwner =
                    actor->AsActorValueOwner();

                if (!avOwner)
                {
                    logger::error(
                        "JumpRequirements: [{}] ActorValueOwner is NULL",
                        section);

                    return false;
                }

                const float stamina =
                    avOwner->GetActorValue(
                        RE::ActorValue::kStamina);

                if (stamina < g_minStamina)
                {
                    if (g_debugLogging)
                    {
                        logger::info(
                            "JumpRequirements: [{}] BLOCKED - Stamina {:.1f}/{:.1f}",
                            section,
                            stamina,
                            g_minStamina);
                    }

                    return false;
                }
            }

            //
            // Perk requirement
            //
            if (!requirement.perkPlugin.empty() &&
                requirement.perkFormID != 0)
            {
                if (!requirement.perk)
                {
                    return false;
                }

                return actor->HasPerk(requirement.perk);
            }

            //
            // Skill level requirement
            //
            auto *avOwner =
                actor->AsActorValueOwner();

            if (!avOwner)
            {
                logger::error(
                    "JumpRequirements: [{}] ActorValueOwner is NULL",
                    section);

                return false;
            }

            const float skill =
                avOwner->GetActorValue(g_skillType);

            return skill >= requirement.level;
        }
    }

    void Load()
    {
        const std::string path =
            "Data/SKSE/Plugins/JumpRequirements.ini";

        logger::info(
            "JumpRequirements: Loading '{}'",
            path);

        CSimpleIniA ini;
        ini.SetUnicode();

        const auto result =
            ini.LoadFile(path.c_str());

        if (result < 0)
        {
            logger::error(
                "JumpRequirements: FAILED to load INI '{}'",
                path);

            return;
        }

        g_debugLogging =
            ini.GetBoolValue(
                "Debug",
                "EnableLogging",
                false);

        const char *skillType =
            ini.GetValue(
                "Settings",
                "SkillType",
                "Sneak");

        g_skillType =
            ParseSkillType(skillType);

        //
        // Global minimum stamina
        //
        g_minStamina =
            static_cast<float>(
                ini.GetDoubleValue(
                    "Settings",
                    "MinStamina",
                    0.0));

        g_minStamina =
            (std::max)(0.0f,
                       g_minStamina);

        logger::info(
            "JumpRequirements: Skill type = '{}'",
            skillType);

        logger::info(
            "JumpRequirements: Minimum stamina = {:.1f}",
            g_minStamina);

        logger::info(
            "JumpRequirements: Debug logging = {}",
            g_debugLogging ? "ON" : "OFF");

        //
        // Charge Jump
        //
        LoadRequirement(
            ini,
            "ChargeJump",
            g_chargeJumpRequirement);

        //
        // Wall Jump
        //
        LoadRequirement(
            ini,
            "WallJump",
            g_wallJumpRequirement);

        g_wallJumpStaggerCooldown =
            static_cast<float>(
                ini.GetDoubleValue(
                    "WallJump",
                    "StaggerCooldown",
                    5.0));

        //
        // Enemy Stagger Jump
        //
        LoadRequirement(
            ini,
            "EnemyStaggerJump",
            g_enemyStaggerJumpRequirement);

        //
        // Double Jump
        //
        LoadRequirement(
            ini,
            "DoubleJump",
            g_doubleJumpRequirement);

        //
        // Double Jump / Air Jump count
        //
        g_maxAirJumps =
            static_cast<int>(
                ini.GetLongValue(
                    "DoubleJump",
                    "MaxAirJumps",
                    1));

        g_maxAirJumps =
            std::clamp(
                g_maxAirJumps,
                1,
                10);

        logger::info(
            "JumpRequirements: [DoubleJump] MaxAirJumps={}",
            g_maxAirJumps);

        //
        // Resolve perk references
        //
        ResolveRequirementPerk(
            "ChargeJump",
            g_chargeJumpRequirement);

        ResolveRequirementPerk(
            "WallJump",
            g_wallJumpRequirement);

        ResolveRequirementPerk(
            "EnemyStaggerJump",
            g_enemyStaggerJumpRequirement);

        ResolveRequirementPerk(
            "DoubleJump",
            g_doubleJumpRequirement);

        logger::info(
            "JumpRequirements: Initialization complete");
    }

    bool MeetsChargeJumpRequirement(RE::Actor *actor)
    {
        return MeetsRequirementInternal(
            actor,
            "ChargeJump",
            g_chargeJumpRequirement);
    }

    bool MeetsWallJumpRequirement(RE::Actor *actor)
    {
        return MeetsRequirementInternal(
            actor,
            "WallJump",
            g_wallJumpRequirement);
    }

    bool MeetsEnemyStaggerJumpRequirement(RE::Actor *actor)
    {
        return MeetsRequirementInternal(
            actor,
            "EnemyStaggerJump",
            g_enemyStaggerJumpRequirement);
    }

    bool MeetsDoubleJumpRequirement(RE::Actor *actor)
    {
        return MeetsRequirementInternal(
            actor,
            "DoubleJump",
            g_doubleJumpRequirement);
    }

    const Requirement &GetChargeJumpRequirement()
    {
        return g_chargeJumpRequirement;
    }

    const Requirement &GetWallJumpRequirement()
    {
        return g_wallJumpRequirement;
    }

    const Requirement &GetEnemyStaggerJumpRequirement()
    {
        return g_enemyStaggerJumpRequirement;
    }

    const Requirement &GetDoubleJumpRequirement()
    {
        return g_doubleJumpRequirement;
    }

    float GetWallJumpStaggerCooldown()
    {
        return g_wallJumpStaggerCooldown;
    }

    int GetMaxAirJumps()
    {
        return g_maxAirJumps;
    }

    float GetMinStamina()
    {
        return g_minStamina;
    }
}