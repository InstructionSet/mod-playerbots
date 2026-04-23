#include "NewRpgDoQuestHelpers.h"

#include <unordered_map>

#include "Log.h"
#include "Logging/Log.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "SharedDefines.h"
#include "Timer.h"

namespace NewRpgDoQuestHelpers
{
namespace
{
struct DoQuestDebugThrottleState
{
    std::string lastMessage;
    uint32 lastLogMs = 0;
};
}  // namespace

bool IsDoQuestDebugEnabled(PlayerbotAI* botAI)
{
    return botAI->HasStrategy("debug do quest", BOT_STATE_NON_COMBAT) ||
           botAI->HasStrategy("debug do quest", BOT_STATE_COMBAT) ||
           botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT) ||
           botAI->HasStrategy("debug rpg", BOT_STATE_COMBAT);
}

void TellDoQuestDebug(PlayerbotAI* botAI, Player* bot, std::string const& message)
{
    if (!IsDoQuestDebugEnabled(botAI))
        return;

    static std::unordered_map<uint64, DoQuestDebugThrottleState> throttleByBot;

    uint32 const nowMs = getMSTime();
    uint64 const botGuidRaw = bot->GetGUID().GetRawValue();
    DoQuestDebugThrottleState& state = throttleByBot[botGuidRaw];

    if (state.lastMessage == message && state.lastLogMs != 0 && (nowMs - state.lastLogMs) < 1500)
        return;

    state.lastMessage = message;
    state.lastLogMs = nowMs;

    sLog->outMessage("playerbots", LogLevel::LOG_LEVEL_DEBUG, "[New RPG][DoQuestDebug] {}", message);

    std::string const chatMessage = "DoQuestDebug: " + message;
    if (!botAI->TellMasterNoFacing(chatMessage) && !botAI->GetMaster())
        bot->Say(chatMessage, bot->GetTeamId() == TEAM_ALLIANCE ? LANG_COMMON : LANG_ORCISH);
}

const char* DoQuestPhaseName(DoQuestPhase phase)
{
    switch (phase)
    {
        case DoQuestPhase::SelectObjectivePOI:
            return "SelectObjectivePOI";
        case DoQuestPhase::TravelToObjectivePOI:
            return "TravelToObjectivePOI";
        case DoQuestPhase::ExecuteObjective:
            return "ExecuteObjective";
        case DoQuestPhase::WaitOrRotateObjective:
            return "WaitOrRotateObjective";
        case DoQuestPhase::SelectRewardPOI:
            return "SelectRewardPOI";
        case DoQuestPhase::TravelToRewardPOI:
            return "TravelToRewardPOI";
        case DoQuestPhase::WaitForTurnIn:
            return "WaitForTurnIn";
        default:
            return "Unknown";
    }
}

void SetDoQuestPhase(PlayerbotAI* botAI, Player* bot, uint32 questId, DoQuestPhase newPhase)
{
    if (botAI->rpgInfo.do_quest.phase == newPhase)
        return;

    botAI->rpgInfo.do_quest.phase = newPhase;
    botAI->rpgInfo.do_quest.phaseStartMs = getMSTime();
    botAI->rpgInfo.do_quest.stagnantTicks = 0;

    TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) + " phase -> " +
                                     DoQuestPhaseName(newPhase));

    NewRpgInfo::DoQuest const& doQuest = botAI->rpgInfo.do_quest;
    float poiDistance = doQuest.pos == WorldPosition() ? -1.0f : bot->GetDistance(doQuest.pos);
    uint32 lastReachAge = doQuest.lastReachPOI ? GetMSTimeDiffToNow(doQuest.lastReachPOI) : 0;
    uint32 trackedTargetEntry = doQuest.lastTrackedTarget.IsEmpty() ? 0 : doQuest.lastTrackedTarget.GetEntry();

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " snapshot: obj=" +
                         std::to_string(doQuest.objectiveIdx) + ", poiDist=" + std::to_string(poiDistance) +
                         ", lastReachAgeMs=" + std::to_string(lastReachAge) + ", stagnant=" +
                         std::to_string(doQuest.stagnantTicks) + ", trackedEntry=" +
                         std::to_string(trackedTargetEntry));
}

bool ShouldIssueQuestChase(NewRpgInfo::DoQuest& doQuest, ObjectGuid targetGuid, float targetDistance,
                           uint32 minIntervalMs, float progressEpsilon)
{
    uint32 nowMs = getMSTime();

    if (doQuest.lastTrackedTarget != targetGuid)
    {
        doQuest.lastTrackedTarget = targetGuid;
        doQuest.lastTrackedDistance = targetDistance;
        doQuest.stagnantTicks = 0;
        doQuest.lastMoveIssueMs = nowMs;
        return true;
    }

    if (targetDistance + progressEpsilon < doQuest.lastTrackedDistance)
    {
        doQuest.lastTrackedDistance = targetDistance;
        doQuest.stagnantTicks = 0;
    }
    else
    {
        ++doQuest.stagnantTicks;
    }

    if (GetMSTimeDiffToNow(doQuest.lastMoveIssueMs) < minIntervalMs)
        return false;

    doQuest.lastMoveIssueMs = nowMs;
    return true;
}

}  // namespace NewRpgDoQuestHelpers
