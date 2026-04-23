#include "NewRpgQuestPositioningAction.h"

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

namespace
{
    constexpr float objectiveDriftBuffer = 35.0f;
}

using namespace NewRpgDoQuestHelpers;

bool NewRpgQuestPositioningAction::Execute(Event event)
{
    uint32 questId = RPG_INFO(do_quest, questId);
    if (bot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
        return false;

    if (EvaluateObjectivePOISelection(questId))
        return true;

    int32 currentObjective = botAI->rpgInfo.do_quest.objectiveIdx;
    return EvaluateObjectiveTravel(questId, currentObjective);
}

bool NewRpgDoIncompleteQuestAction::EvaluateObjectivePOISelection(uint32 questId)
{
    if (botAI->rpgInfo.do_quest.pos != WorldPosition())
        return false;

    if (!SelectIncompleteObjectivePOI(questId))
        return true;

    return false;
}

bool NewRpgDoIncompleteQuestAction::EvaluateObjectiveTravel(uint32 questId, int32 currentObjective)
{
    return EnforceObjectiveLeash(questId, currentObjective);
}

float NewRpgDoIncompleteQuestAction::GetObjectiveLeashDistance() const
{
    return sPlayerbotAIConfig->sightDistance;
}

bool NewRpgDoIncompleteQuestAction::EnforceObjectiveLeash(uint32 questId, int32 currentObjective)
{
    float objectiveLeashDistance = GetObjectiveLeashDistance();
    float const preReachLeashDistance = 25.0f;
    float currentPoiDistance = bot->GetDistance(botAI->rpgInfo.do_quest.pos);
    bool hasReachedPOI = botAI->rpgInfo.do_quest.lastReachPOI != 0;

    if (hasReachedPOI && currentPoiDistance > (objectiveLeashDistance + objectiveDriftBuffer))
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                             std::to_string(currentObjective) + " drifted " +
                             std::to_string(currentPoiDistance) +
                             " yards from POI after reach, resetting POI reach state");
        botAI->rpgInfo.do_quest.lastReachPOI = 0;
        hasReachedPOI = false;
    }

    if (!hasReachedPOI && currentPoiDistance <= preReachLeashDistance)
        return false;

    if (hasReachedPOI)
        return false;

    if (!bot->IsInCombat())
    {
        Unit* grindTarget = AI_VALUE(Unit*, "grind target");
        Unit* currentTarget = AI_VALUE(Unit*, "current target");
        if (grindTarget || currentTarget)
        {
            TellDoQuestDebug(botAI, bot,
                             bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                                 std::to_string(currentObjective) +
                                 " clearing stale combat handoff while traveling to objective POI");
            context->GetValue<Unit*>("grind target")->Set(nullptr);
            context->GetValue<Unit*>("current target")->Set(nullptr);
        }
    }

    SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::TravelToObjectivePOI);
    bool moved = MoveFarTo(botAI->rpgInfo.do_quest.pos);
    if (!moved)
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) +
                             " travel to objective POI deferred (movement cooldown/path wait)");
        return true;
    }
    return moved;
}

bool NewRpgDoIncompleteQuestAction::SelectIncompleteObjectivePOI(uint32 questId)
{
    SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::SelectObjectivePOI);

    std::vector<POIInfo> poiInfo;
    if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo))
    {
        botAI->lowPriorityQuest.insert(questId);
        if (TrySwitchToAnotherIncompleteQuest(questId, "has no valid incomplete POI"))
            return false;

        TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) +
                                        " has no valid incomplete POI and no fallback quest, switch to IDLE");
        botAI->rpgInfo.ChangeToIdle();
        return false;
    }

    uint32 nearestIdx = 0;
    float nearestDistance = std::numeric_limits<float>::max();
    for (uint32 i = 0; i < poiInfo.size(); ++i)
    {
        float distance = bot->GetDistance2d(poiInfo[i].pos.x, poiInfo[i].pos.y);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearestIdx = i;
        }
    }

    constexpr float poiAvoidRadius = 25.0f;
    uint32 selectedIdx = nearestIdx;
    if (poiInfo.size() > 1 && nearestDistance < poiAvoidRadius)
    {
        uint32 farIdx = nearestIdx;
        float farNearestDist = std::numeric_limits<float>::max();
        bool foundFar = false;
        for (uint32 i = 0; i < poiInfo.size(); ++i)
        {
            float distance = bot->GetDistance2d(poiInfo[i].pos.x, poiInfo[i].pos.y);
            if (distance >= poiAvoidRadius && distance < farNearestDist)
            {
                farNearestDist = distance;
                farIdx = i;
                foundFar = true;
            }
        }
        if (foundFar)
        {
            TellDoQuestDebug(botAI, bot,
                             bot->GetName() + " quest " + std::to_string(questId) +
                                 " nearest POI is " + std::to_string(nearestDistance) +
                                 " yards away (likely abandoned POI), preferring farther candidate at " +
                                 std::to_string(farNearestDist) + " yards");
            selectedIdx = farIdx;
        }
    }

    G3D::Vector2 nearestPoi = poiInfo[selectedIdx].pos;
    int32 objectiveIdx = poiInfo[selectedIdx].objectiveIdx;

    float dx = nearestPoi.x, dy = nearestPoi.y;
    float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(dx, dy));

    if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
    {
        TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) +
                                        " invalid terrain height at selected POI, retry next tick");
        return false;
    }

    WorldPosition pos(bot->GetMapId(), dx, dy, dz);
    botAI->rpgInfo.do_quest.lastReachPOI = 0;
    botAI->rpgInfo.do_quest.pos = pos;
    botAI->rpgInfo.do_quest.objectiveIdx = objectiveIdx;
    if (!bot->IsInCombat())
    {
        context->GetValue<Unit*>("grind target")->Set(nullptr);
        context->GetValue<Unit*>("current target")->Set(nullptr);
    }
    SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::TravelToObjectivePOI);
    float selectedDistance = bot->GetDistance2d(nearestPoi.x, nearestPoi.y);
    TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) +
                    " selected objective " + std::to_string(objectiveIdx) +
                    " at distance " + std::to_string(selectedDistance) + " from " +
                    std::to_string(poiInfo.size()) + " candidates");
    return true;
}
