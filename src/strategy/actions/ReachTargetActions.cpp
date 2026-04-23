/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "ReachTargetActions.h"

#include "Event.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "TargetValue.h"

bool ReachTargetAction::Execute(Event event) { return ReachCombatTo(AI_VALUE(Unit*, GetTargetName()), distance); }

bool ReachTargetAction::isUseful()
{
    // do not move while staying
    if (botAI->HasStrategy("stay", botAI->GetState()))
    {
        TargetingDebugHelper::Log(botAI, std::string("reach ") + getName() + " blocked: stay strategy active");
        return false;
    }

    // do not move while casting
    if (bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL) != nullptr)
    {
        TargetingDebugHelper::Log(botAI, std::string("reach ") + getName() + " blocked: channeling spell");
        return false;
    }
    Unit* target = GetTarget();
    if (!target)
    {
        TargetingDebugHelper::Log(botAI, std::string("reach ") + getName() + " blocked: no target from " + GetTargetName());
        return false;
    }

    if (GetTargetName() == "current target" && TargetingRiskHelper::ShouldAvoidApproach(botAI, target, distance))
    {
        float targetRisk = TargetingRiskHelper::CalculateSelectionRisk(botAI, target);
        Unit* saferTarget = botAI->GetAiObjectContext()->GetValue<Unit*>("dps target")->Get();
        std::string saferName = saferTarget && saferTarget != target ? saferTarget->GetName() : "none";
        float saferRisk = saferTarget && saferTarget != target ? TargetingRiskHelper::CalculateSelectionRisk(botAI, saferTarget) : 0.0f;
        TargetingDebugHelper::Log(botAI,
                                  std::string("reach ") + getName() + " blocked: unsafe approach to " +
                                      target->GetName() + " risk=" + std::to_string(targetRisk) + " safer=" +
                                      saferName + " saferRisk=" + std::to_string(saferRisk));
        return false;
    }

    if (bot->IsWithinCombatRange(target, distance))
    {
        TargetingDebugHelper::Log(botAI, std::string("reach ") + getName() + " blocked: already in combat range of " +
                                             target->GetName());
        return false;
    }

    // float dis = distance + CONTACT_DISTANCE;
    TargetingDebugHelper::Log(botAI, std::string("reach ") + getName() + " useful: target=" + target->GetName() +
                                         " dist=" + std::to_string(bot->GetDistance(target)));
    return true;  // sServerFacade->IsDistanceGreaterThan(AI_VALUE2(float,
                  // "distance", GetTargetName()), distance);
}

std::string const ReachTargetAction::GetTargetName() { return "current target"; }

bool CastReachTargetSpellAction::isUseful()
{
    // do not move while staying
    if (botAI->HasStrategy("stay", botAI->GetState()))
    {
        return false;
    }

    return sServerFacade->IsDistanceGreaterThan(AI_VALUE2(float, "distance", "current target"),
                                                (distance + sPlayerbotAIConfig->contactDistance));
}

ReachSpellAction::ReachSpellAction(PlayerbotAI* botAI)
    : ReachTargetAction(botAI, "reach spell", botAI->GetRange("spell"))
{
}

ReachPartyMemberToHealAction::ReachPartyMemberToHealAction(PlayerbotAI* botAI)
    : ReachTargetAction(botAI, "reach party member to heal", botAI->GetRange("heal"))
{
}

std::string const ReachPartyMemberToHealAction::GetTargetName() { return "party member to heal"; }

ReachPartyMemberToResurrectAction::ReachPartyMemberToResurrectAction(PlayerbotAI* botAI)
    : ReachTargetAction(botAI, "reach party member to resurrect", botAI->GetRange("spell"))
{
}

std::string const ReachPartyMemberToResurrectAction::GetTargetName() { return "party member to resurrect"; }
