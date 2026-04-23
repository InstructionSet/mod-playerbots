/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TargetValue.h"

#include <algorithm>
#include <unordered_map>

#include "AttackersValue.h"
#include "LastMovementValue.h"
#include "ObjectGuid.h"
#include "Playerbots.h"
#include "RtiTargetValue.h"
#include "ScriptedCreature.h"
#include "ThreatMgr.h"

namespace
{
float GetRankWeight(Unit* unit)
{
    Creature* creature = unit ? unit->ToCreature() : nullptr;
    if (!creature)
        return 1.0f;

    switch (creature->GetCreatureTemplate()->rank)
    {
        case CREATURE_ELITE_RARE:
            return 1.5f;
        case CREATURE_ELITE_ELITE:
            return 2.25f;
        case CREATURE_ELITE_RAREELITE:
            return 2.5f;
        case CREATURE_ELITE_WORLDBOSS:
            return 8.0f;
        default:
            return 1.0f;
    }
}

float GetRelativeUnitWeight(Player* bot, Unit* unit)
{
    if (!bot || !unit)
        return 0.0f;

    float botLevel = std::max(1.0f, static_cast<float>(bot->GetLevel()));
    float relativeLevel = static_cast<float>(unit->GetLevel()) / botLevel;
    relativeLevel = std::max(0.5f, std::min(2.5f, relativeLevel));
    return relativeLevel * GetRankWeight(unit);
}

float GetDistanceToSegment2d(float ax, float ay, float bx, float by, float px, float py)
{
    float abx = bx - ax;
    float aby = by - ay;
    float apx = px - ax;
    float apy = py - ay;
    float abLengthSq = abx * abx + aby * aby;
    if (abLengthSq <= 0.0f)
        return std::sqrt(apx * apx + apy * apy);

    float projection = (apx * abx + apy * aby) / abLengthSq;
    projection = std::max(0.0f, std::min(1.0f, projection));

    float nearestX = ax + projection * abx;
    float nearestY = ay + projection * aby;
    float dx = px - nearestX;
    float dy = py - nearestY;
    return std::sqrt(dx * dx + dy * dy);
}

bool IsSoloSafetyEnabled(Player* bot)
{
    return bot && !bot->GetGroup();
}

struct TargetDebugThrottleState
{
    std::string lastThrottleKey;
    uint32 lastLogMs = 0;
};

std::string BuildTargetDebugThrottleKey(std::string const& message)
{
    std::string key = message;

    // Reach-action logs can include numeric values that fluctuate slightly.
    // Collapse those to a stable key so we only log state changes.
    if (key.rfind("reach ", 0) == 0)
    {
        size_t metricPos = key.find(" risk=");
        if (metricPos == std::string::npos)
            metricPos = key.find(" dist=");
        if (metricPos != std::string::npos)
            key = key.substr(0, metricPos);
    }

    return key;
}
}

Unit* FindTargetStrategy::GetResult() { return result; }

Unit* TargetValue::FindTarget(FindTargetStrategy* strategy)
{
    GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        ThreatMgr& ThreatMgr = unit->GetThreatMgr();
        strategy->CheckAttacker(unit, &ThreatMgr);
    }

    return strategy->GetResult();
}

bool FindNonCcTargetStrategy::IsCcTarget(Unit* attacker)
{
    if (Group* group = botAI->GetBot()->GetGroup())
    {
        Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player* member = ObjectAccessor::FindPlayer(itr->guid);
            if (!member || !member->IsAlive())
                continue;

            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(member))
            {
                if (botAI->GetAiObjectContext()->GetValue<Unit*>("rti cc target")->Get() == attacker)
                    return true;

                std::string const rti = botAI->GetAiObjectContext()->GetValue<std::string>("rti cc")->Get();
                int32 index = RtiTargetValue::GetRtiIndex(rti);
                if (index != -1)
                {
                    if (ObjectGuid guid = group->GetTargetIcon(index))
                        if (attacker->GetGUID() == guid)
                            return true;
                }
            }
        }

        if (ObjectGuid guid = group->GetTargetIcon(4))
            if (attacker->GetGUID() == guid)
                return true;
    }

    return false;
}

void FindTargetStrategy::GetPlayerCount(Unit* creature, uint32* tankCount, uint32* dpsCount)
{
    Player* bot = botAI->GetBot();
    if (tankCountCache.find(creature) != tankCountCache.end())
    {
        *tankCount = tankCountCache[creature];
        *dpsCount = dpsCountCache[creature];
        return;
    }

    *tankCount = 0;
    *dpsCount = 0;

    Unit::AttackerSet attackers(creature->getAttackers());
    for (Unit* attacker : attackers)
    {
        if (!attacker || !attacker->IsAlive() || attacker == bot)
            continue;

        Player* player = attacker->ToPlayer();
        if (!player)
            continue;

        if (botAI->IsTank(player))
            ++(*tankCount);
        else
            ++(*dpsCount);
    }

    tankCountCache[creature] = *tankCount;
    dpsCountCache[creature] = *dpsCount;
}

bool FindTargetStrategy::IsHighPriority(Unit* attacker)
{
    if (Group* group = botAI->GetBot()->GetGroup())
    {
        ObjectGuid guid = group->GetTargetIcon(7);
        if (guid && attacker->GetGUID() == guid)
        {
            return true;
        }
    }
    GuidVector prioritizedTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Get();
    for (ObjectGuid targetGuid : prioritizedTargets)
    {
        if (targetGuid && attacker->GetGUID() == targetGuid)
        {
            return true;
        }
    }
    return false;
}

WorldPosition LastLongMoveValue::Calculate()
{
    LastMovement& lastMove = *context->GetValue<LastMovement&>("last movement");
    if (lastMove.lastPath.empty())
        return WorldPosition();

    return lastMove.lastPath.getBack();
}

WorldPosition HomeBindValue::Calculate()
{
    return WorldPosition(bot->m_homebindMapId, bot->m_homebindX, bot->m_homebindY, bot->m_homebindZ, 0.f);
}

Unit* FindTargetValue::Calculate()
{
    if (qualifier == "")
    {
        return nullptr;
    }
    Group* group = bot->GetGroup();
    if (!group)
    {
        return nullptr;
    }
    HostileReference* ref = bot->getHostileRefMgr().getFirst();
    while (ref)
    {
        ThreatMgr* threatManager = ref->GetSource();
        Unit* unit = threatManager->GetOwner();
        std::wstring wnamepart;
        Utf8toWStr(unit->GetName(), wnamepart);
        wstrToLower(wnamepart);
        if (!qualifier.empty() && qualifier.length() == wnamepart.length() && Utf8FitTo(qualifier, wnamepart))
        {
            return unit;
        }
        ref = ref->next();
    }
    return nullptr;
}

float TargetingRiskHelper::CalculateSelectionRisk(PlayerbotAI* botAI, Unit* target)
{
    if (!botAI || !target)
        return 0.0f;

    Player* bot = botAI->GetBot();
    if (!IsSoloSafetyEnabled(bot) || !target->IsAlive())
        return 0.0f;

    float risk = 0.0f;
    float clusterRadius = std::max(8.0f, sPlayerbotAIConfig->aoeRadius * 1.5f);
    float aggroRadius = std::max(clusterRadius + 2.0f, sPlayerbotAIConfig->aggroDistance);
    float corridorRadius = std::max(4.0f, sPlayerbotAIConfig->meleeDistance + 1.0f);

    GuidVector possibleTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("possible targets no los")->Get();
    for (ObjectGuid const& guid : possibleTargets)
    {
        Unit* nearby = botAI->GetUnit(guid);
        if (!nearby || nearby == target || !nearby->IsAlive() || nearby->GetMapId() != target->GetMapId())
            continue;

        float weight = GetRelativeUnitWeight(bot, nearby);
        if (weight <= 0.0f)
            continue;

        bool unengaged = !nearby->GetTarget() && !nearby->GetThreatMgr().getCurrentVictim();
        if (unengaged)
            weight *= 1.35f;

        float distanceToTarget = target->GetDistance(nearby);
        if (distanceToTarget <= clusterRadius)
            risk += weight * 1.5f;
        else if (distanceToTarget <= aggroRadius)
            risk += weight * 0.75f;

        float distanceToPath = GetDistanceToSegment2d(bot->GetPositionX(), bot->GetPositionY(), target->GetPositionX(),
                                                      target->GetPositionY(), nearby->GetPositionX(), nearby->GetPositionY());
        if (distanceToPath <= corridorRadius && bot->GetDistance(nearby) + 1.0f < bot->GetDistance(target))
            risk += weight * (unengaged ? 1.1f : 0.65f);
    }

    float preferredRange = botAI->IsRanged(bot) ? sPlayerbotAIConfig->spellDistance : sPlayerbotAIConfig->meleeDistance;
    float travelDistance = bot->GetDistance(target);
    if (travelDistance > preferredRange)
        risk += std::min(2.0f, (travelDistance - preferredRange) / 20.0f);

    Unit* victim = target->GetVictim();
    if (victim == bot || target->GetThreatMgr().GetThreat(bot) > 0.0f)
        risk *= 0.85f;

    return risk;
}

bool TargetingRiskHelper::ShouldAvoidApproach(PlayerbotAI* botAI, Unit* target, float distance)
{
    if (!botAI || !target)
        return false;

    Player* bot = botAI->GetBot();
    if (!IsSoloSafetyEnabled(bot) || bot->IsWithinCombatRange(target, distance))
        return false;

    float targetRisk = CalculateSelectionRisk(botAI, target);
    Unit* saferTarget = botAI->GetAiObjectContext()->GetValue<Unit*>("dps target")->Get();
    if (!saferTarget || saferTarget == target)
        return false;

    float saferRisk = CalculateSelectionRisk(botAI, saferTarget);
    return saferRisk + 1.25f < targetRisk;
}

bool TargetingDebugHelper::IsEnabled(PlayerbotAI* botAI)
{
    return botAI &&
           (botAI->HasStrategy("debug move", BOT_STATE_NON_COMBAT) ||
            botAI->HasStrategy("debug move", BOT_STATE_COMBAT) ||
            botAI->HasStrategy("debug", BOT_STATE_NON_COMBAT) ||
            botAI->HasStrategy("debug", BOT_STATE_COMBAT));
}

void TargetingDebugHelper::Log(PlayerbotAI* botAI, std::string const& message)
{
    if (!IsEnabled(botAI))
        return;

    Player* bot = botAI->GetBot();
    if (!bot)
        return;

    static std::unordered_map<uint64, TargetDebugThrottleState> stateByBot;

    uint64 botGuidRaw = bot->GetGUID().GetRawValue();
    TargetDebugThrottleState& state = stateByBot[botGuidRaw];
    uint32 nowMs = getMSTime();
    std::string throttleKey = BuildTargetDebugThrottleKey(message);
    uint32 throttleMs = throttleKey.rfind("reach ", 0) == 0 ? 3000 : 1000;

    if (state.lastThrottleKey == throttleKey && nowMs - state.lastLogMs < throttleMs)
        return;

    state.lastThrottleKey = throttleKey;
    state.lastLogMs = nowMs;

    LOG_DEBUG("playerbots", "[TargetDebug] {} {}", bot->GetName().c_str(), message.c_str());
}

void FindBossTargetStrategy::CheckAttacker(Unit* attacker, ThreatMgr* threatManager)
{
    UnitAI* unitAI = attacker->GetAI();
    BossAI* bossAI = dynamic_cast<BossAI*>(unitAI);
    if (bossAI)
    {
        result = attacker;
    }
}

Unit* BossTargetValue::Calculate()
{
    FindBossTargetStrategy strategy(botAI);
    return FindTarget(&strategy);
}