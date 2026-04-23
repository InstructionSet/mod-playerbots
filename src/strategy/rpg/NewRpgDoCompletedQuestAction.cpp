#include "NewRpgDoCompletedQuestAction.h"

#include <algorithm>

#include "BroadcastHelper.h"
#include "IVMapMgr.h"
#include "Log.h"
#include "Logging/Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Playerbots.h"

#include "NewRpgDoQuestHelpers.h"

using namespace NewRpgDoQuestHelpers;

bool NewRpgDoCompletedQuestAction::Execute(Event /*event*/)
{
    uint32 questId = RPG_INFO(quest, questId);
    if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
        return false;

    const Quest* quest = RPG_INFO(quest, quest);
    bool stageResult = false;

    if (EvaluateCompletedRewardPOISelection(questId, quest, stageResult))
        return stageResult;

    if (EvaluateCompletedRewardTurnIn(questId, stageResult))
        return stageResult;

    return false;
}

bool NewRpgDoCompletedQuestAction::EvaluateCompletedRewardPOISelection(uint32 questId, Quest const* quest,
                                                                       bool& stageResult)
{
    stageResult = false;

    if (RPG_INFO(quest, objectiveIdx) == -1)
        return false;

    if (!SelectRewardPOI(questId, quest))
    {
        stageResult = false;
        return true;
    }

    return false;
}

bool NewRpgDoCompletedQuestAction::EvaluateCompletedRewardTurnIn(uint32 questId, bool& stageResult)
{
    stageResult = HandleRewardPOI(questId);
    return true;
}

bool NewRpgDoCompletedQuestAction::SelectRewardPOI(uint32 questId, Quest const* quest)
{
    SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::SelectRewardPOI);

    TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) +
                                    " completed, resolving reward POI");
    BroadcastHelper::BroadcastQuestUpdateComplete(botAI, bot, quest);
    botAI->rpgStatistic.questCompleted++;

    std::vector<POIInfo> poiInfo;
    if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo, true))
    {
        TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) +
                                        " no reward POI found despite complete status");
        botAI->lowPriorityQuest.insert(questId);
        if (TrySwitchToAnotherIncompleteQuest(questId, "has no reward POI"))
            return false;

        TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) +
                                        " no fallback incomplete quest found, switch to IDLE");
        botAI->rpgInfo.ChangeToIdle();
        return false;
    }

    assert(poiInfo.size() > 0);
    float dx = poiInfo[0].pos.x, dy = poiInfo[0].pos.y;
    float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(dx, dy));

    if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
    {
        TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) +
                                        " invalid terrain height at reward POI, retry next tick");
        return false;
    }

    WorldPosition pos(bot->GetMapId(), dx, dy, dz);
    botAI->rpgInfo.do_quest.lastReachPOI = 0;
    botAI->rpgInfo.do_quest.pos = pos;
    botAI->rpgInfo.do_quest.objectiveIdx = -1;
    SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::TravelToRewardPOI);
    TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) + " reward POI selected");
    return true;
}

bool NewRpgDoCompletedQuestAction::HandleRewardPOI(uint32 questId)
{
    if (botAI->rpgInfo.do_quest.pos == WorldPosition())
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " reward branch has empty POI");
        return false;
    }

    if (bot->GetDistance(botAI->rpgInfo.do_quest.pos) > 10.0f && !botAI->rpgInfo.do_quest.lastReachPOI)
    {
        SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::TravelToRewardPOI);
        bool moved = MoveFarTo(botAI->rpgInfo.do_quest.pos);
        if (!moved)
        {
            TellDoQuestDebug(botAI, bot,
                             bot->GetName() + " quest " + std::to_string(questId) +
                                 " travel to reward POI deferred (movement cooldown/path wait)");
        }
        return moved;
    }

    if (!botAI->rpgInfo.do_quest.lastReachPOI)
    {
        SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::WaitForTurnIn);
        botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " reached reward POI, waiting for turn-in");
        return true;
    }

    SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::WaitForTurnIn);

    if (GetMSTimeDiffToNow(botAI->rpgInfo.do_quest.lastReachPOI) >= poiStayTime)
    {
        botAI->lowPriorityQuest.insert(questId);
        botAI->rpgStatistic.questAbandoned++;
        sLog->outMessage("playerbots", LogLevel::LOG_LEVEL_DEBUG, "[New RPG] {} marked as abandoned quest {}",
                         bot->GetName(), questId);
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " timed out at reward POI");
        if (TrySwitchToAnotherIncompleteQuest(questId, "timed out at reward POI"))
            return true;

        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) +
                             " no fallback incomplete quest found, switch to IDLE");
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }
    return false;
}
