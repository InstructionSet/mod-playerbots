#include "NewRpgQuestLifecycleAction.h"

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

bool NewRpgQuestLifecycleAction::Execute(Event event)
{
    uint32 questId = RPG_INFO(do_quest, questId);
    if (bot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
        return false;

    EvaluateObjectivePreSync(questId);
    return EvaluateLifecycleRecovery(questId);
}

bool NewRpgDoIncompleteQuestAction::EvaluateObjectivePreSync(uint32 questId)
{
    if (botAI->rpgInfo.do_quest.pos == WorldPosition())
        return false;

    CheckAndClearCompletedObjective(questId);
    return false;
}

bool NewRpgDoIncompleteQuestAction::EvaluateLifecycleRecovery(uint32 questId)
{
    int32 currentObjective = botAI->rpgInfo.do_quest.objectiveIdx;
    uint32 stagnant = botAI->rpgInfo.do_quest.stagnantTicks;

    constexpr uint32 targetReselectTicks = 1800;
    constexpr uint32 localRescoutTicks = 2400;
    constexpr uint32 rotatePoiTicks = 3000;

    if (stagnant >= rotatePoiTicks)
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                             std::to_string(currentObjective) + " extreme stagnation (" +
                             std::to_string(stagnant) + " ticks, ~5min), clearing POI for rotation");
        botAI->rpgInfo.do_quest.pos = WorldPosition();
        botAI->rpgInfo.do_quest.lastReachPOI = 0;
        ResetTrackedObjectiveTarget();
        return true;
    }

    if (stagnant >= localRescoutTicks)
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                             std::to_string(currentObjective) + " sustained stagnation (" +
                             std::to_string(stagnant) + " ticks, ~4min), resetting POI reach state");
        ResetTrackedObjectiveTarget();
        botAI->rpgInfo.do_quest.lastReachPOI = 0;
        return true;
    }

    if (stagnant >= targetReselectTicks)
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                             std::to_string(currentObjective) + " stagnation limit (" +
                             std::to_string(stagnant) + " ticks, ~3min), resetting target tracking");
        ResetTrackedObjectiveTarget();
        return true;
    }

    if (stagnant > 0 && stagnant % 5 == 0)
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                             std::to_string(currentObjective) + " chase throttled, stagnantTicks " +
                             std::to_string(stagnant));
    }

    if (botAI->rpgInfo.do_quest.pos == WorldPosition())
        return false;

    float currentPoiDistance = bot->GetDistance(botAI->rpgInfo.do_quest.pos);
    if (!botAI->rpgInfo.do_quest.lastReachPOI)
    {
        if (currentPoiDistance <= 25.0f)
        {
            SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::WaitOrRotateObjective);
            botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();
            TellDoQuestDebug(botAI, bot,
                             bot->GetName() + " quest " + std::to_string(questId) +
                                 " reached objective POI, starting stay/progression timer");
            return true;
        }

        return false;
    }

    if (GetMSTimeDiffToNow(botAI->rpgInfo.do_quest.lastReachPOI) < poiStayTime)
        return false;

    SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::WaitOrRotateObjective);
    if (!HasObjectiveProgress(questId, currentObjective))
    {
        botAI->lowPriorityQuest.insert(questId);
        botAI->rpgStatistic.questAbandoned++;
        sLog->outMessage("playerbots", LogLevel::LOG_LEVEL_DEBUG, "[New RPG] {} marked as abandoned quest {}",
                         bot->GetName(), questId);
        TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) + " timed out at objective " +
                                        std::to_string(currentObjective) + " with no progression");
        if (TrySwitchToAnotherIncompleteQuest(questId, "timed out at objective " + std::to_string(currentObjective)))
            return true;

        TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) +
                                        " no fallback incomplete quest found, switch to IDLE");
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }

    TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                                    std::to_string(currentObjective) +
                                    " had progression, rotating to another POI");
    botAI->rpgInfo.do_quest.lastReachPOI = 0;
    botAI->rpgInfo.do_quest.pos = WorldPosition();
    botAI->rpgInfo.do_quest.objectiveIdx = 0;
    return true;
}

void NewRpgDoIncompleteQuestAction::CheckAndClearCompletedObjective(uint32 questId)
{
    int32 currentObjective = botAI->rpgInfo.do_quest.objectiveIdx;
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);
    bool completed = true;
    if (currentObjective < QUEST_OBJECTIVES_COUNT)
    {
        if (q_status.CreatureOrGOCount[currentObjective] < quest->RequiredNpcOrGoCount[currentObjective])
            completed = false;
    }
    else if (currentObjective < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
    {
        if (q_status.ItemCount[currentObjective - QUEST_OBJECTIVES_COUNT] <
            quest->RequiredItemCount[currentObjective - QUEST_OBJECTIVES_COUNT])
            completed = false;
    }

    if (completed)
    {
        TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                                        std::to_string(currentObjective) + " completed, clearing active POI");
        botAI->rpgInfo.do_quest.lastReachPOI = 0;
        botAI->rpgInfo.do_quest.pos = WorldPosition();
        botAI->rpgInfo.do_quest.objectiveIdx = 0;
    }
}

void NewRpgDoIncompleteQuestAction::ResetTrackedObjectiveTarget()
{
    botAI->rpgInfo.do_quest.lastTrackedTarget = ObjectGuid();
    botAI->rpgInfo.do_quest.lastTrackedDistance = FLT_MAX;
    botAI->rpgInfo.do_quest.stagnantTicks = 0;
}

bool NewRpgDoIncompleteQuestAction::HasObjectiveProgress(uint32 questId, int32 currentObjective) const
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return false;

    const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);
    if (currentObjective < QUEST_OBJECTIVES_COUNT)
    {
        return q_status.CreatureOrGOCount[currentObjective] != 0 && quest->RequiredNpcOrGoCount[currentObjective];
    }

    if (currentObjective < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
    {
        uint32 itemIdx = currentObjective - QUEST_OBJECTIVES_COUNT;
        return q_status.ItemCount[itemIdx] != 0 && quest->RequiredItemCount[itemIdx];
    }

    return false;
}
