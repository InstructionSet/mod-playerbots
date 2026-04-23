#include "NewRpgQuestExecutionAction.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

#include "DBCStores.h"
#include "GameObject.h"
#include "G3D/Vector2.h"
#include "IVMapMgr.h"
#include "Log.h"
#include "Logging/Log.h"
#include "LootObjectStack.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Playerbots.h"
#include "Random.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "World.h"

using namespace NewRpgDoQuestHelpers;

bool NewRpgQuestExecutionAction::Execute(Event event)
{
    uint32 questId = RPG_INFO(do_quest, questId);
    if (bot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
        return false;

    if (ExecuteObjectiveAtPOI(questId))
        return true;

    return false;
}

bool NewRpgDoIncompleteQuestAction::YieldForNearbyLoot(uint32 questId, int32 currentObjective)
{
    if (bot->IsInCombat())
        return false;

    LootObjectStack* lootStack = AI_VALUE(LootObjectStack*, "available loot");
    if (!lootStack)
        return false;

    GuidVector corpses = AI_VALUE(GuidVector, "nearest corpses");
    for (ObjectGuid const& guid : corpses)
        lootStack->Add(guid);

    GuidVector gameObjects = AI_VALUE(GuidVector, "nearest game objects");
    for (ObjectGuid const& guid : gameObjects)
        lootStack->Add(guid);

    bool canLoot = AI_VALUE(bool, "can loot");
    bool hasAvailableLoot = AI_VALUE(bool, "has available loot");

    // Do not pause quest combat handoff for far/stale loot candidates.
    // Only yield when loot can actually be opened right now.
    if (!canLoot)
        return false;

    context->GetValue<Unit*>("current target")->Set(nullptr);

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                         std::to_string(currentObjective) + " pausing grind handoff for loot (canLoot=" +
                         (canLoot ? "true" : "false") + ", hasAvailableLoot=" +
                         (hasAvailableLoot ? "true" : "false") + ")");
    return true;
}

bool NewRpgDoIncompleteQuestAction::ExecuteObjectiveAtPOI(uint32 questId)
{
    SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::ExecuteObjective);

    int32 currentObjective = botAI->rpgInfo.do_quest.objectiveIdx;
    if (currentObjective >= 0 && currentObjective < QUEST_OBJECTIVES_COUNT)
    {
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (quest)
        {
            int32 requiredNpcOrGo = quest->RequiredNpcOrGo[currentObjective];
            if (requiredNpcOrGo < 0)
                return HandleGameObjectObjective(questId, currentObjective, static_cast<uint32>(std::abs(requiredNpcOrGo)));
            else if (requiredNpcOrGo > 0)
                return HandleCreatureObjective(questId, currentObjective, static_cast<uint32>(requiredNpcOrGo));
        }
    }
    else if (currentObjective >= QUEST_OBJECTIVES_COUNT &&
             currentObjective < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
    {
        return HandleItemObjective(questId, currentObjective);
    }

    return false;
}

bool NewRpgDoIncompleteQuestAction::HandleGameObjectObjective(uint32 questId, int32 currentObjective, uint32 requiredGoEntry)
{
    GuidVector possibleGameObjects = AI_VALUE(GuidVector, "possible new rpg game objects");
    if (HandleGameObjectObjectiveEntries(questId, currentObjective, requiredGoEntry, possibleGameObjects,
                                         "possible new rpg game objects"))
        return true;

    GuidVector nearbyGameObjects = AI_VALUE(GuidVector, "nearest game objects no los");
    if (HandleGameObjectObjectiveEntries(questId, currentObjective, requiredGoEntry, nearbyGameObjects,
                                         "nearest game objects no los"))
        return true;

    return false;
}

bool NewRpgDoIncompleteQuestAction::HandleGameObjectObjectiveEntries(uint32 questId, int32 currentObjective, uint32 requiredGoEntry,
                                                           GuidVector const& gameObjects, std::string const& sourceTag)
{
    // --- Step 1: Find the nearest spawned GO matching the required entry ---
    uint32 matchedEntryCount = 0;
    float nearestMatchedDistance = std::numeric_limits<float>::max();
    ObjectGuid nearestMatchedGuid;

    for (ObjectGuid const& guid : gameObjects)
    {
        GameObject* go = ObjectAccessor::GetGameObject(*bot, guid);
        if (!go || !go->isSpawned() || go->GetEntry() != requiredGoEntry)
            continue;

        ++matchedEntryCount;
        float distance = bot->GetDistance(go);
        if (distance < nearestMatchedDistance)
        {
            nearestMatchedDistance = distance;
            nearestMatchedGuid = guid;
        }
    }

    if (!matchedEntryCount)
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                             std::to_string(currentObjective) + " GO entry " +
                             std::to_string(requiredGoEntry) + " not found in " + sourceTag + " (" +
                             std::to_string(gameObjects.size()) + " candidates)");
        return false;
    }

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                         std::to_string(currentObjective) + " GO entry " + std::to_string(requiredGoEntry) +
                         " matched " + std::to_string(matchedEntryCount) + " object(s) in " + sourceTag +
                         ", nearest distance " + std::to_string(nearestMatchedDistance) +
                         (nearestMatchedDistance > INTERACTION_DISTANCE ? " > " : " <= ") +
                         "interact range " + std::to_string(INTERACTION_DISTANCE));

    if (!nearestMatchedGuid)
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                             std::to_string(currentObjective) + " GO entry " + std::to_string(requiredGoEntry) +
                             " matched candidates but no nearest guid was selected in " + sourceTag +
                             " (matched=" + std::to_string(matchedEntryCount) +
                             ", nearestDistance=" + std::to_string(nearestMatchedDistance) +
                             "). This usually means distance comparison never updated (e.g. invalid/NaN distance).");
        return false;
    }

    // --- Step 2: Postpone if already in combat ---
    if (bot->IsInCombat())
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                             std::to_string(currentObjective) +
                             " is in combat; postponing GO movement until combat ends");
        return false;
    }

    // --- Step 3: Approach the GO and let regular loot flow perform open/loot ---
    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                         std::to_string(currentObjective) + " [GO branch:move-to-target] moving to GO entry " +
                         std::to_string(requiredGoEntry));

    // Clear stale grind/current target so they don't pull the bot away during GO approach.
    context->GetValue<Unit*>("grind target")->Set(nullptr);
    context->GetValue<Unit*>("current target")->Set(nullptr);

    float const openRange = INTERACTION_DISTANCE - 2.0f;
    if (!MoveWorldObjectTo(nearestMatchedGuid, openRange))
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                             std::to_string(currentObjective) +
                             " [GO branch:move-deferred] movement cooldown/path wait; keeping GO objective authority");
    }

    return true;
}

bool NewRpgDoIncompleteQuestAction::FindNearestObjectiveGameObject(uint32 /*questId*/, int32 /*currentObjective*/,
                                                                    uint32 requiredGoEntry,
                                                                    GuidVector const& gameObjects,
                                                                    ObjectGuid& nearestGuid,
                                                                    float& nearestDistance) const
{
    nearestGuid.Clear();
    nearestDistance = std::numeric_limits<float>::max();

    for (ObjectGuid const& guid : gameObjects)
    {
        GameObject* go = ObjectAccessor::GetGameObject(*bot, guid);
        if (!go || !go->isSpawned() || go->GetEntry() != requiredGoEntry)
            continue;

        float const distance = bot->GetDistance(go);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearestGuid = guid;
        }
    }

    return static_cast<bool>(nearestGuid);
}

std::vector<uint32> NewRpgDoIncompleteQuestAction::FindNearbyGoForRequiredItem(uint32 itemId)
{
    std::vector<uint32> matchingGoEntries;

    GuidVector nearbyGameObjects = context->GetValue<GuidVector>("nearest game objects no los")->Get();

    for (ObjectGuid const& guid : nearbyGameObjects)
    {
        GameObject* go = ObjectAccessor::GetGameObject(*bot, guid);
        if (!go || !go->isSpawned())
            continue;

        uint32 goEntry = go->GetEntry();
        GameObjectQuestItemList const* questItems = sObjectMgr->GetGameObjectQuestItemList(goEntry);
        if (!questItems)
            continue;

        for (size_t i = 0; i < questItems->size(); ++i)
        {
            uint32 questItemId = uint32((*questItems)[i]);
            if (questItemId == itemId)
            {
                matchingGoEntries.push_back(goEntry);
                break;
            }
        }
    }

    return matchingGoEntries;
}

bool NewRpgDoIncompleteQuestAction::HandleCreatureObjective(uint32 questId, int32 currentObjective,
                                                            uint32 requiredCreatureEntry)
{
    GuidVector possibleTargets = AI_VALUE(GuidVector, "possible targets");
    ObjectGuid nearestMatchingTarget;
    float nearestDistance = std::numeric_limits<float>::max();

    for (ObjectGuid const& guid : possibleTargets)
    {
        Unit* target = ObjectAccessor::GetUnit(*bot, guid);
        if (!target || !target->IsAlive() || target->GetEntry() != requiredCreatureEntry)
            continue;

        float const distance = bot->GetDistance(target);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearestMatchingTarget = guid;
        }
    }

    if (!nearestMatchingTarget)
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                             std::to_string(currentObjective) + " found no matching creature entry " +
                             std::to_string(requiredCreatureEntry) + " in " +
                             std::to_string(possibleTargets.size()) + " possible targets");
        return false;
    }

    Unit* objectiveTarget = ObjectAccessor::GetUnit(*bot, nearestMatchingTarget);
    float combatDistance = botAI->IsCaster(bot) ? sPlayerbotAIConfig->spellDistance : INTERACTION_DISTANCE;
    if (objectiveTarget && nearestDistance > combatDistance)
    {
        if (!bot->IsInCombat())
            return YieldTargetToAttackAnythingFlow(questId, "objective", currentObjective, objectiveTarget, false);

        if (!ShouldIssueQuestChase(botAI->rpgInfo.do_quest, nearestMatchingTarget, nearestDistance))
            return false;

        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                             std::to_string(currentObjective) + " deferring chase movement to combat strategy");

        return true;
    }

    if (!objectiveTarget)
        return false;

    if (!botAI->rpgInfo.do_quest.lastReachPOI)
        botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();

    ResetTrackedObjectiveTarget();

    if (!bot->IsInCombat())
        return YieldTargetToAttackAnythingFlow(questId, "objective", currentObjective, objectiveTarget, true);

    context->GetValue<Unit*>("current target")->Set(objectiveTarget);

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                         std::to_string(currentObjective) + " assigned objective target to combat strategy");
    return true;
}

bool NewRpgDoIncompleteQuestAction::ShouldDeferItemObjectiveUntilPoiArrival(uint32 questId,
                                                                            int32 currentObjective)
{
    WorldPosition poi(botAI->rpgInfo.do_quest.pos);
    if (poi == WorldPosition() || poi.getMapId() != bot->GetMapId() || botAI->rpgInfo.do_quest.lastReachPOI)
        return false;

    float const preReachLeashDistance = 25.0f;
    float const poiDistance = bot->GetDistance(poi);
    if (poiDistance <= preReachLeashDistance)
        return false;

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                         std::to_string(currentObjective) +
                         " deferring item-objective execution while traveling to POI (distance " +
                         std::to_string(poiDistance) + ")");
    return true;
}

Quest const* NewRpgDoIncompleteQuestAction::LoadItemObjectiveRequirements(uint32 questId, int32 currentObjective,
                                                                          uint32& requiredItemId,
                                                                          uint32& requiredItemCount)
    {
    requiredItemId = 0;
    requiredItemCount = 0;

    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return nullptr;

    uint32 itemObjectiveIndex = currentObjective - QUEST_OBJECTIVES_COUNT;
    requiredItemId = quest->RequiredItemId[itemObjectiveIndex];
    requiredItemCount = quest->RequiredItemCount[itemObjectiveIndex];

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                         std::to_string(currentObjective) + " requires item " +
                         std::to_string(requiredItemId) + " count " +
                         std::to_string(requiredItemCount) +
                         ", checking for nearby GOs containing this item");

    return quest;
}

bool NewRpgDoIncompleteQuestAction::TryHandleItemObjectiveViaNearbyGos(uint32 questId, int32 currentObjective,
                                                                        uint32 requiredItemId,
                                                                        bool& shouldContinue)
{
    shouldContinue = true;

    std::vector<uint32> matchingGoEntries = FindNearbyGoForRequiredItem(requiredItemId);
    if (matchingGoEntries.empty())
        return false;

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                         std::to_string(currentObjective) + " found " +
                         std::to_string(matchingGoEntries.size()) + " nearby GO(s) containing item " +
                         std::to_string(requiredItemId));

    uint32 targetGoEntry = matchingGoEntries[0];
    GuidVector nearbyGameObjects = AI_VALUE(GuidVector, "nearest game objects no los");

    GuidVector matchingGos;
    for (ObjectGuid const& guid : nearbyGameObjects)
    {
        GameObject* go = ObjectAccessor::GetGameObject(*bot, guid);
        if (go && go->GetEntry() == targetGoEntry)
            matchingGos.push_back(guid);
    }

    if (matchingGos.empty())
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                             std::to_string(currentObjective) +
                             " [Item branch:phase1-go-entry-matched-no-instance] falling back to phase2");
        return false;
    }

    shouldContinue = false;
    if (HandleGameObjectObjectiveEntries(questId, currentObjective, targetGoEntry, matchingGos,
                                         "item-objective target GOs"))
        return true;

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                         std::to_string(currentObjective) +
                         " [Item branch:phase1-go-deferred] returning before phase2 fallback");
    return false;
}

bool NewRpgDoIncompleteQuestAction::ShouldSkipItemObjectiveGrindFallbackAtPoi(uint32 questId, int32 currentObjective,
                                                                               uint32 requiredItemId)
{
    GuidVector nearbyGameObjects = AI_VALUE(GuidVector, "nearest game objects no los");
    if (nearbyGameObjects.empty() || bot->GetDistance(botAI->rpgInfo.do_quest.pos) > 25.0f)
        return false;

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                         std::to_string(currentObjective) + " found " +
                         std::to_string(nearbyGameObjects.size()) + " nearby GOs at POI but none contain item " +
                         std::to_string(requiredItemId) + ", skipping grind target");
    return true;
}

Unit* NewRpgDoIncompleteQuestAction::ResolveItemObjectiveGrindTarget(uint32 questId, int32 currentObjective)
{
    Unit* grindTarget = AI_VALUE(Unit*, "grind target");
    if (botAI->rpgInfo.do_quest.lastTrackedTarget.IsEmpty())
        return grindTarget;

    Unit* stickyTarget = ObjectAccessor::GetUnit(*bot, botAI->rpgInfo.do_quest.lastTrackedTarget);
    if (!stickyTarget || !stickyTarget->IsAlive())
        return grindTarget;

    if (!grindTarget || stickyTarget->GetGUID() != grindTarget->GetGUID())
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                             std::to_string(currentObjective) + " keeping sticky target entry " +
                             std::to_string(stickyTarget->GetEntry()) + " at distance " +
                             std::to_string(bot->GetDistance(stickyTarget)));
    }

    return stickyTarget;
}

bool NewRpgDoIncompleteQuestAction::HandleItemObjectiveWithGrindTarget(uint32 questId, int32 currentObjective,
                                                                       Unit* grindTarget)
{
    context->GetValue<Unit*>("grind target")->Set(grindTarget);
    if (bot->IsInCombat())
        context->GetValue<Unit*>("current target")->Set(grindTarget);

    float targetDistance = bot->GetDistance(grindTarget);
    float combatDistance = botAI->IsCaster(bot) ? sPlayerbotAIConfig->spellDistance : INTERACTION_DISTANCE;

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                         std::to_string(currentObjective) + " using grind target entry " +
                         std::to_string(grindTarget->GetEntry()) + " at distance " +
                         std::to_string(targetDistance));

    if (targetDistance > combatDistance)
    {
        if (!bot->IsInCombat())
            return YieldTargetToAttackAnythingFlow(questId, "item objective", currentObjective, grindTarget, false);

        if (!ShouldIssueQuestChase(botAI->rpgInfo.do_quest, grindTarget->GetGUID(), targetDistance))
            return false;

        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                             std::to_string(currentObjective) +
                             " deferring chase movement to combat strategy");

        return true;
    }

    if (!botAI->rpgInfo.do_quest.lastReachPOI)
        botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();

    ResetTrackedObjectiveTarget();

    if (!bot->IsInCombat())
        return YieldTargetToAttackAnythingFlow(questId, "item objective", currentObjective, grindTarget, true);

    context->GetValue<Unit*>("current target")->Set(grindTarget);

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                         std::to_string(currentObjective) + " assigned grind target to combat strategy");
    return true;
}

bool NewRpgDoIncompleteQuestAction::HandleItemObjective(uint32 questId, int32 currentObjective)
{
    if (ShouldDeferItemObjectiveUntilPoiArrival(questId, currentObjective))
        return false;

    uint32 requiredItemId = 0;
    uint32 requiredItemCount = 0;
    Quest const* quest =
        LoadItemObjectiveRequirements(questId, currentObjective, requiredItemId, requiredItemCount);

    bool shouldContinue = true;
    if (TryHandleItemObjectiveViaNearbyGos(questId, currentObjective, requiredItemId, shouldContinue))
        return true;
    if (!shouldContinue)
        return false;

    if (ShouldSkipItemObjectiveGrindFallbackAtPoi(questId, currentObjective, requiredItemId))
        return false;

    Unit* grindTarget = ResolveItemObjectiveGrindTarget(questId, currentObjective);
    if (grindTarget)
        return HandleItemObjectiveWithGrindTarget(questId, currentObjective, grindTarget);

    if (quest)
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                             std::to_string(currentObjective) + " found no grind target for item " +
                             std::to_string(requiredItemId) + " at current POI");
    }

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                         std::to_string(currentObjective) + " scouting around POI for quest-drop mobs");
    return MoveRandomNear(35.0f);
}

bool NewRpgDoIncompleteQuestAction::YieldTargetToAttackAnythingFlow(uint32 questId, std::string const& objectiveLabel,
                                                          int32 currentObjective, Unit* target, bool inRange)
{
    context->GetValue<Unit*>("grind target")->Set(target);
    context->GetValue<Unit*>("current target")->Set(target);

    if (Action* attackAnything = botAI->GetAiObjectContext()->GetAction("attack anything"))
    {
        if (attackAnything->Execute(Event("new rpg pinned threat handoff")))
        {
            TellDoQuestDebug(botAI, bot,
                             bot->GetName() + " quest " + std::to_string(questId) + " " + objectiveLabel + " " +
                                 std::to_string(currentObjective) +
                                 " executed immediate attack-anything on pinned target entry " +
                                 std::to_string(target->GetEntry()));
            return true;
        }
    }

    // Avoid suppressing no-target triggers if immediate attack handoff could not start.
    context->GetValue<Unit*>("current target")->Set(nullptr);

    bool const targetAlive = target && target->IsAlive();
    bool const targetInWorld = target && target->IsInWorld();
    bool const targetHostile = target && bot->IsHostileTo(target);
    bool const targetInLos = target && bot->IsWithinLOSInMap(target);
    bool const botInCombat = bot->IsInCombat();

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " " + objectiveLabel + " " +
                         std::to_string(currentObjective) +
                         " immediate attack-anything failed (alive=" + (targetAlive ? "true" : "false") +
                         ", inWorld=" + (targetInWorld ? "true" : "false") +
                         ", hostile=" + (targetHostile ? "true" : "false") +
                         ", inLos=" + (targetInLos ? "true" : "false") +
                         ", botInCombat=" + (botInCombat ? "true" : "false") + ") " +
                         (inRange ? " seeded in-range grind target and yielded for attack-anything flow"
                                  : " seeded grind target and yielded for attack-anything flow"));
    return false;
}
