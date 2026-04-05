#include "NewRpgAction.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <unordered_map>

#include "BroadcastHelper.h"
#include "ChatHelper.h"
#include "Chat.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "G3D/Vector2.h"
#include "GossipDef.h"
#include "IVMapMgr.h"
#include "LootObjectStack.h"
#include "NewRpgInfo.h"
#include "NewRpgStrategy.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Position.h"
#include "QuestDef.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"
#include "SharedDefines.h"
#include "StatsWeightCalculator.h"
#include "Timer.h"
#include "TravelMgr.h"
#include "World.h"

namespace
{
    struct DoQuestDebugThrottleState
    {
        std::string lastMessage;
        uint32 lastLogMs = 0;
    };

    bool IsDoQuestDebugEnabled(PlayerbotAI* botAI)
    {
        return botAI->HasStrategy("debug do quest", BOT_STATE_NON_COMBAT) ||
               botAI->HasStrategy("debug do quest", BOT_STATE_COMBAT) ||
               botAI->HasStrategy("debug quest", BOT_STATE_NON_COMBAT) ||
               botAI->HasStrategy("debug rpg", BOT_STATE_COMBAT);
    }

    std::string TrimCopy(std::string value)
    {
        auto isSpace = [](unsigned char c) { return std::isspace(c); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char c) { return !isSpace(c); }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [&](char c) { return !isSpace(c); }).base(), value.end());
        return value;
    }

    std::string NormalizeStatusToken(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            if (c == '-' || c == ' ')
                return '_';
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    bool TryParseUInt32(std::string const& text, uint32& out)
    {
        if (text.empty())
            return false;

        char* endPtr = nullptr;
        unsigned long parsed = std::strtoul(text.c_str(), &endPtr, 10);
        if (endPtr == text.c_str() || *endPtr != '\0' || parsed > std::numeric_limits<uint32>::max())
            return false;

        out = static_cast<uint32>(parsed);
        return true;
    }

    void TellDoQuestDebug(PlayerbotAI* botAI, Player* bot, std::string const& message)
    {
        if (!IsDoQuestDebugEnabled(botAI))
            return;

        static std::unordered_map<uint64, DoQuestDebugThrottleState> throttleByBot;

        uint32 const nowMs = getMSTime();
        uint64 const botGuidRaw = bot->GetGUID().GetRawValue();
        DoQuestDebugThrottleState& state = throttleByBot[botGuidRaw];

        // Drop identical messages repeated in a tight loop to keep logs readable.
        if (state.lastMessage == message && state.lastLogMs != 0 && (nowMs - state.lastLogMs) < 1500)
            return;

        state.lastMessage = message;
        state.lastLogMs = nowMs;

        LOG_DEBUG("playerbots", "[New RPG][DoQuestDebug] {}", message);

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
                               uint32 minIntervalMs = 700, float progressEpsilon = 0.75f)
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
}

static void SendRpgReply(Player* bot, Player* owner, std::string const& message)
{
    if (!bot || !owner)
        return;

    // In self-bot mode, self-whispers are treated as command input and get swallowed.
    // Send direct notifications to the owner instead.
    if (owner == bot)
    {
        ChatHandler(owner->GetSession()).PSendSysMessage("{}", message.c_str());
        return;
    }

    bot->Whisper(message, LANG_UNIVERSAL, owner);
}

bool TellRpgStatusAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;
    SendRpgReply(bot, owner, botAI->rpgInfo.ToString());
    return true;
}

bool StartRpgDoQuestAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;

    std::string const text = event.getParam();
    PlayerbotChatHandler ch(owner);
    uint32 questId = ch.extractQuestId(text);
    const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
    if (quest)
    {
        botAI->rpgInfo.ChangeToDoQuest(questId, quest);
        SendRpgReply(bot, owner, "Start to do quest " + std::to_string(questId));
        return true;
    }
    SendRpgReply(bot, owner, "Invalid quest " + text);
    return false;
}

bool SetRpgStatusAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;

    std::string text = TrimCopy(event.getParam());
    if (text.empty())
    {
        SendRpgReply(bot, owner,
                     "Usage: new rpg set <idle|rest|go_grind|go_camp|wander_random|wander_npc|do_quest|travel_flight>");
        return false;
    }

    std::istringstream stream(text);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token)
        tokens.push_back(token);

    if (tokens.empty())
        return false;

    std::string status = NormalizeStatusToken(tokens[0]);
    size_t index = 1;
    if (status == "go" && index < tokens.size())
    {
        status = NormalizeStatusToken(tokens[0] + "_" + tokens[1]);
        ++index;
    }
    else if (status == "wander" && index < tokens.size())
    {
        status = NormalizeStatusToken(tokens[0] + "_" + tokens[1]);
        ++index;
    }
    else if (status == "do" && index < tokens.size())
    {
        status = NormalizeStatusToken(tokens[0] + "_" + tokens[1]);
        ++index;
    }
    else if (status == "travel" && index < tokens.size())
    {
        status = NormalizeStatusToken(tokens[0] + "_" + tokens[1]);
        ++index;
    }

    if (status == "idle")
    {
        botAI->rpgInfo.ChangeToIdle();
        SendRpgReply(bot, owner, "New RPG status set to IDLE");
        return true;
    }

    if (status == "rest")
    {
        botAI->rpgInfo.ChangeToRest();
        bot->SetStandState(UNIT_STAND_STATE_SIT);
        SendRpgReply(bot, owner, "New RPG status set to REST");
        return true;
    }

    if (status == "wander_random" || status == "wander")
    {
        if (RandomChangeStatus({RPG_WANDER_RANDOM}))
        {
            SendRpgReply(bot, owner, "New RPG status set to WANDER_RANDOM");
            return true;
        }

        SendRpgReply(bot, owner, "Unable to set WANDER_RANDOM right now");
        return false;
    }

    if (status == "wander_npc")
    {
        if (RandomChangeStatus({RPG_WANDER_NPC}))
        {
            SendRpgReply(bot, owner, "New RPG status set to WANDER_NPC");
            return true;
        }

        SendRpgReply(bot, owner, "Unable to set WANDER_NPC right now");
        return false;
    }

    if (status == "go_grind" || status == "grind")
    {
        if (RandomChangeStatus({RPG_GO_GRIND}))
        {
            SendRpgReply(bot, owner, "New RPG status set to GO_GRIND");
            return true;
        }

        SendRpgReply(bot, owner, "Unable to set GO_GRIND right now");
        return false;
    }

    if (status == "go_camp" || status == "camp")
    {
        if (RandomChangeStatus({RPG_GO_CAMP}))
        {
            SendRpgReply(bot, owner, "New RPG status set to GO_CAMP");
            return true;
        }

        SendRpgReply(bot, owner, "Unable to set GO_CAMP right now");
        return false;
    }

    if (status == "do_quest")
    {
        if (index < tokens.size())
        {
            uint32 questId = 0;
            if (TryParseUInt32(tokens[index], questId))
            {
                const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
                if (!quest)
                {
                    SendRpgReply(bot, owner, "Invalid quest id");
                    return false;
                }

                if (bot->GetQuestStatus(questId) == QUEST_STATUS_NONE)
                {
                    SendRpgReply(bot, owner, "Quest is not in log");
                    return false;
                }

                botAI->rpgInfo.ChangeToDoQuest(questId, quest);
                SendRpgReply(bot, owner, "New RPG status set to DO_QUEST");
                return true;
            }
        }

        if (RandomChangeStatus({RPG_DO_QUEST}))
        {
            SendRpgReply(bot, owner, "New RPG status set to DO_QUEST");
            return true;
        }

        SendRpgReply(bot, owner, "Unable to set DO_QUEST right now");
        return false;
    }

    if (status == "travel_flight")
    {
        if (RandomChangeStatus({RPG_TRAVEL_FLIGHT}))
        {
            SendRpgReply(bot, owner, "New RPG status set to TRAVEL_FLIGHT");
            return true;
        }

        SendRpgReply(bot, owner, "Unable to set TRAVEL_FLIGHT right now");
        return false;
    }

    SendRpgReply(bot, owner, "Unknown RPG status. Try: rpg help");
    return false;
}

bool HelpRpgStatusAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;

    SendRpgReply(bot, owner, "New RPG commands: new rpg status | new rpg set <status> | new rpg reset");
    SendRpgReply(bot, owner, "Compatibility aliases: rpg status | rpg set | rpg reset");
    SendRpgReply(bot, owner,
                 "Statuses: idle, rest, go_grind, go_camp, wander_random, wander_npc, do_quest [questId], travel_flight");
    return true;
}

bool ResetRpgStatusAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;

    botAI->rpgInfo.ChangeToIdle();
    SendRpgReply(bot, owner, "New RPG status reset to IDLE");
    return true;
}

bool NewRpgStatusUpdateAction::Execute(Event event)
{
    NewRpgInfo& info = botAI->rpgInfo;
    switch (info.status)
    {
        case RPG_IDLE:
        {
            return RandomChangeStatus({RPG_GO_CAMP, RPG_GO_GRIND, RPG_WANDER_RANDOM, RPG_WANDER_NPC, RPG_DO_QUEST,
                                       RPG_TRAVEL_FLIGHT, RPG_REST});
        }
        case RPG_GO_GRIND:
        {
            WorldPosition& originalPos = info.go_grind.pos;
            assert(info.go_grind.pos != WorldPosition());
            // GO_GRIND -> WANDER_RANDOM
            if (bot->GetExactDist(originalPos) < 10.0f)
            {
                info.ChangeToWanderRandom();
                return true;
            }
            break;
        }
        case RPG_GO_CAMP:
        {
            WorldPosition& originalPos = info.go_camp.pos;
            assert(info.go_camp.pos != WorldPosition());
            // GO_CAMP -> WANDER_NPC
            if (bot->GetExactDist(originalPos) < 10.0f)
            {
                info.ChangeToWanderNpc();
                return true;
            }
            break;
        }
        case RPG_WANDER_RANDOM:
        {
            // WANDER_RANDOM -> IDLE
            if (info.HasStatusPersisted(statusWanderRandomDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_WANDER_NPC:
        {
            if (info.HasStatusPersisted(statusWanderNpcDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_DO_QUEST:
        {
            // DO_QUEST -> IDLE
            if (info.HasStatusPersisted(statusDoQuestDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_TRAVEL_FLIGHT:
        {
            if (info.flight.inFlight && !bot->IsInFlight())
            {
                // flight arrival
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_REST:
        {
            // REST -> IDLE
            if (info.HasStatusPersisted(statusRestDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        default:
            break;
    }
    return false;
}

bool NewRpgGoGrindAction::Execute(Event event)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    return MoveFarTo(botAI->rpgInfo.go_grind.pos);
}

bool NewRpgGoCampAction::Execute(Event event)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    return MoveFarTo(botAI->rpgInfo.go_camp.pos);
}

bool NewRpgWanderRandomAction::Execute(Event event)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    return MoveRandomNear();
}

bool NewRpgWanderNpcAction::Execute(Event event)
{
    NewRpgInfo& info = botAI->rpgInfo;
    if (!info.wander_npc.npcOrGo)
    {
        // No npc can be found, switch to IDLE
        ObjectGuid npcOrGo = ChooseNpcOrGameObjectToInteract();
        if (npcOrGo.IsEmpty())
        {
            info.ChangeToIdle();
            return true;
        }
        info.wander_npc.npcOrGo = npcOrGo;
        info.wander_npc.lastReach = 0;
        return true;
    }

    WorldObject* object = ObjectAccessor::GetWorldObject(*bot, info.wander_npc.npcOrGo);
    if (object && IsWithinInteractionDist(object))
    {
        if (!info.wander_npc.lastReach)
        {
            info.wander_npc.lastReach = getMSTime();
            if (bot->CanInteractWithQuestGiver(object))
                InteractWithNpcOrGameObjectForQuest(info.wander_npc.npcOrGo);
            return true;
        }

        if (info.wander_npc.lastReach && GetMSTimeDiffToNow(info.wander_npc.lastReach) < npcStayTime)
            return false;

        // has reached the npc for more than `npcStayTime`, select the next target
        info.wander_npc.npcOrGo = ObjectGuid();
        info.wander_npc.lastReach = 0;
    }
    else
    {
        return MoveWorldObjectTo(info.wander_npc.npcOrGo);
    }
    return true;
}

bool NewRpgDoQuestAction::Execute(Event event)
{
    uint32 questId = RPG_INFO(quest, questId);
    uint8 questStatus = bot->GetQuestStatus(questId);

    if (questStatus == QUEST_STATUS_COMPLETE && RPG_INFO(quest, objectiveIdx) != -1)
    {
        if (TryDeferTurnInForNearbyObjective(questId))
            return true;
    }

    if (SearchQuestGiverAndAcceptOrReward())
    {
        TellDoQuestDebug(botAI, bot, bot->GetName() + " delayed by questgiver interaction");
        return true;
    }

    switch (questStatus)
    {
        case QUEST_STATUS_INCOMPLETE:
            return DoIncompleteQuest();
        case QUEST_STATUS_COMPLETE:
            return DoCompletedQuest();
        default:
            break;
    }

    TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) + " has unsupported status " +
                                    std::to_string(questStatus) + ", switch to IDLE");

    botAI->rpgInfo.ChangeToIdle();
    return true;
}

bool NewRpgDoQuestAction::DoIncompleteQuest()
{
    uint32 questId = RPG_INFO(do_quest, questId);
    if (botAI->rpgInfo.do_quest.pos != WorldPosition())
        CheckAndClearCompletedObjective(questId);

    if (botAI->rpgInfo.do_quest.pos == WorldPosition())
    {
        if (!SelectIncompleteObjectivePOI(questId))
            return true;
    }

    int32 currentObjective = botAI->rpgInfo.do_quest.objectiveIdx;
    float poiTravelDistance = 10.0f;
    if (currentObjective >= QUEST_OBJECTIVES_COUNT &&
        currentObjective < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
    {
        // Item objectives can legitimately roam farther while following viable drop targets.
        poiTravelDistance = sPlayerbotAIConfig->sightDistance;
    }

    float currentPoiDistance = bot->GetDistance(botAI->rpgInfo.do_quest.pos);
    if (botAI->rpgInfo.do_quest.lastReachPOI && currentPoiDistance > (poiTravelDistance + 35.0f))
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                             std::to_string(currentObjective) + " drifted " +
                             std::to_string(currentPoiDistance) +
                             " yards from POI after reach, resetting POI reach state");
        botAI->rpgInfo.do_quest.lastReachPOI = 0;
    }

    if (currentPoiDistance > poiTravelDistance && !botAI->rpgInfo.do_quest.lastReachPOI)
    {
        SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::TravelToObjectivePOI);
        bool moved = MoveFarTo(botAI->rpgInfo.do_quest.pos);
        if (!moved)
        {
            TellDoQuestDebug(botAI, bot,
                             bot->GetName() + " quest " + std::to_string(questId) +
                                 " travel to objective POI deferred (movement cooldown/path wait)");
        }
        return moved;
    }

    if (ExecuteObjectiveAtPOI(questId))
        return true;

    Unit* pendingGrindTarget = AI_VALUE(Unit*, "grind target");
    if (!bot->IsInCombat() && pendingGrindTarget && pendingGrindTarget->IsAlive())
    {
        if (YieldForNearbyLoot(questId, currentObjective))
            return true;

        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                             std::to_string(currentObjective) +
                             " pending grind target handoff, yielding before wait/rotate");
        return false;
    }

    return HandleObjectiveStayAndRotation(questId);
}

bool NewRpgDoQuestAction::YieldForNearbyLoot(uint32 questId, int32 currentObjective)
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
    if (!canLoot && !hasAvailableLoot)
        return false;

    context->GetValue<Unit*>("current target")->Set(nullptr);

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                         std::to_string(currentObjective) + " pausing grind handoff for loot (canLoot=" +
                         (canLoot ? "true" : "false") + ", hasAvailableLoot=" +
                         (hasAvailableLoot ? "true" : "false") + ")");
    return true;
}

void NewRpgDoQuestAction::CheckAndClearCompletedObjective(uint32 questId)
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

bool NewRpgDoQuestAction::SelectIncompleteObjectivePOI(uint32 questId)
{
    SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::SelectObjectivePOI);

    std::vector<POIInfo> poiInfo;
    if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo))
    {
        TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) +
                                        " no valid incomplete POI found, switch to IDLE");
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

    // If the nearest POI is very close (bot may be standing at an abandoned POI),
    // prefer a candidate that is farther away to avoid immediately re-selecting it.
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
    SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::TravelToObjectivePOI);
    float selectedDistance = bot->GetDistance2d(nearestPoi.x, nearestPoi.y);
    TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) +
                    " selected objective " + std::to_string(objectiveIdx) +
                    " at distance " + std::to_string(selectedDistance) + " from " +
                    std::to_string(poiInfo.size()) + " candidates");
    return true;
}

bool NewRpgDoQuestAction::ExecuteObjectiveAtPOI(uint32 questId)
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
            {
                uint32 requiredGoEntry = static_cast<uint32>(std::abs(requiredNpcOrGo));
                auto tryUseMatchingGameObject = [&](GuidVector const& gameObjects, std::string const& sourceTag) -> bool {
                    uint32 matchedEntryCount = 0;
                    float nearestMatchedDistance = std::numeric_limits<float>::max();
                    ObjectGuid nearestMatchedGuid;

                    for (ObjectGuid const& guid : gameObjects)
                    {
                        GameObject* go = ObjectAccessor::GetGameObject(*bot, guid);
                        if (!go || !go->isSpawned())
                            continue;
                        if (go->GetEntry() != requiredGoEntry)
                            continue;

                        ++matchedEntryCount;

                        float distance = bot->GetDistance(go);
                        if (distance < nearestMatchedDistance)
                        {
                            nearestMatchedDistance = distance;
                            nearestMatchedGuid = guid;
                        }

                        if (distance > INTERACTION_DISTANCE)
                            continue;

                        go->Use(bot);
                        TellDoQuestDebug(botAI, bot,
                                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                                             std::to_string(currentObjective) + " used GO entry " +
                                             std::to_string(requiredGoEntry) + " from " + sourceTag);
                        return true;
                    }

                    if (!matchedEntryCount)
                    {
                        TellDoQuestDebug(botAI, bot,
                                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                                             std::to_string(currentObjective) + " GO entry " +
                                             std::to_string(requiredGoEntry) + " not found in " + sourceTag + " (" +
                                             std::to_string(gameObjects.size()) + " candidates)");
                    }
                    else
                    {
                        TellDoQuestDebug(botAI, bot,
                                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                                             std::to_string(currentObjective) + " GO entry " +
                                             std::to_string(requiredGoEntry) + " matched " +
                                             std::to_string(matchedEntryCount) + " object(s) in " + sourceTag +
                                             ", nearest distance " + std::to_string(nearestMatchedDistance) +
                                             " > interact range " + std::to_string(INTERACTION_DISTANCE));

                        if (nearestMatchedGuid)
                        {
                            TellDoQuestDebug(botAI, bot,
                                             bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                                                 std::to_string(currentObjective) + " moving to GO entry " +
                                                 std::to_string(requiredGoEntry));
                            return MoveWorldObjectTo(nearestMatchedGuid, INTERACTION_DISTANCE);
                        }
                    }

                    return false;
                };

                GuidVector possibleGameObjects = AI_VALUE(GuidVector, "possible new rpg game objects");
                if (tryUseMatchingGameObject(possibleGameObjects, "possible new rpg game objects"))
                    return true;

                GuidVector nearbyGameObjects = AI_VALUE(GuidVector, "nearest game objects no los");
                if (tryUseMatchingGameObject(nearbyGameObjects, "nearest game objects no los"))
                    return true;
            }
            else if (requiredNpcOrGo > 0)
            {
                uint32 requiredCreatureEntry = static_cast<uint32>(requiredNpcOrGo);
                GuidVector possibleTargets = AI_VALUE(GuidVector, "possible targets no los");
                ObjectGuid nearestMatchingTarget;
                float nearestDistance = std::numeric_limits<float>::max();

                // Keep chasing an already selected valid target to avoid ping-pong retargeting.
                if (!botAI->rpgInfo.do_quest.lastTrackedTarget.IsEmpty())
                {
                    Unit* stickyTarget = ObjectAccessor::GetUnit(*bot, botAI->rpgInfo.do_quest.lastTrackedTarget);
                    if (stickyTarget && stickyTarget->IsAlive() && stickyTarget->GetEntry() == requiredCreatureEntry)
                    {
                        nearestMatchingTarget = stickyTarget->GetGUID();
                        nearestDistance = bot->GetDistance(stickyTarget);
                    }
                }

                for (ObjectGuid const& guid : possibleTargets)
                {
                    Unit* target = ObjectAccessor::GetUnit(*bot, guid);
                    if (!target || !target->IsAlive())
                        continue;
                    if (target->GetEntry() != requiredCreatureEntry)
                        continue;

                    if (nearestMatchingTarget == target->GetGUID())
                        continue;

                    float distance = bot->GetDistance(target);
                    if (distance < nearestDistance)
                    {
                        nearestDistance = distance;
                        nearestMatchingTarget = guid;
                    }
                }

                if (nearestMatchingTarget)
                {
                    Unit* objectiveTarget = ObjectAccessor::GetUnit(*bot, nearestMatchingTarget);
                    float combatDistance = botAI->IsCaster(bot) ? sPlayerbotAIConfig->spellDistance : INTERACTION_DISTANCE;
                    if (objectiveTarget && nearestDistance > combatDistance)
                    {
                        if (!bot->IsInCombat())
                        {
                            context->GetValue<Unit*>("grind target")->Set(objectiveTarget);
                            context->GetValue<Unit*>("current target")->Set(nullptr);

                            if (YieldForNearbyLoot(questId, currentObjective))
                                return true;

                            TellDoQuestDebug(botAI, bot,
                                             bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                                                 std::to_string(currentObjective) +
                                                 " seeded grind target and yielded for attack-anything flow");
                            return false;
                        }

                        if (!ShouldIssueQuestChase(botAI->rpgInfo.do_quest, nearestMatchingTarget, nearestDistance))
                        {
                            if (botAI->rpgInfo.do_quest.stagnantTicks <= 1 || botAI->rpgInfo.do_quest.stagnantTicks % 5 == 0)
                            {
                                TellDoQuestDebug(botAI, bot,
                                                 bot->GetName() + " quest " + std::to_string(questId) +
                                                     " objective " + std::to_string(currentObjective) +
                                                     " chase throttled for creature entry " +
                                                     std::to_string(requiredCreatureEntry) + ", distance " +
                                                     std::to_string(nearestDistance) + ", stagnantTicks " +
                                                     std::to_string(botAI->rpgInfo.do_quest.stagnantTicks));
                            }

                            uint32 stagnant = botAI->rpgInfo.do_quest.stagnantTicks;
                            constexpr uint32 targetReselectTicks = 1800;  // ~3min at 100ms tick
                            constexpr uint32 localRescoutTicks = 2400;    // ~4min at 100ms tick
                            constexpr uint32 rotatePoiTicks = 3000;       // ~5min at 100ms tick

                            if (stagnant >= rotatePoiTicks)
                            {
                                TellDoQuestDebug(botAI, bot,
                                                 bot->GetName() + " quest " + std::to_string(questId) +
                                                     " objective " + std::to_string(currentObjective) +
                                                     " extreme stagnation (" + std::to_string(stagnant) +
                                                     " ticks, ~5min), clearing POI for rotation");
                                botAI->rpgInfo.do_quest.pos = WorldPosition();
                                botAI->rpgInfo.do_quest.lastReachPOI = 0;
                                botAI->rpgInfo.do_quest.lastTrackedTarget = ObjectGuid();
                                botAI->rpgInfo.do_quest.lastTrackedDistance = FLT_MAX;
                                botAI->rpgInfo.do_quest.stagnantTicks = 0;
                            }
                            else if (stagnant >= localRescoutTicks)
                            {
                                TellDoQuestDebug(botAI, bot,
                                                 bot->GetName() + " quest " + std::to_string(questId) +
                                                     " objective " + std::to_string(currentObjective) +
                                                     " sustained stagnation (" + std::to_string(stagnant) +
                                                     " ticks, ~4min), forcing local re-scout around POI");
                                botAI->rpgInfo.do_quest.lastTrackedTarget = ObjectGuid();
                                botAI->rpgInfo.do_quest.lastTrackedDistance = FLT_MAX;
                                botAI->rpgInfo.do_quest.stagnantTicks = 0;
                                return MoveRandomNear(20.0f);
                            }
                            else if (stagnant >= targetReselectTicks)
                            {
                                TellDoQuestDebug(botAI, bot,
                                                 bot->GetName() + " quest " + std::to_string(questId) +
                                                     " objective " + std::to_string(currentObjective) +
                                                     " stagnation limit (" + std::to_string(stagnant) +
                                                     " ticks, ~3min), resetting target tracking");
                                botAI->rpgInfo.do_quest.lastTrackedTarget = ObjectGuid();
                                botAI->rpgInfo.do_quest.lastTrackedDistance = FLT_MAX;
                                botAI->rpgInfo.do_quest.stagnantTicks = 0;
                            }

                            return true;
                        }

                        TellDoQuestDebug(botAI, bot,
                                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                                             std::to_string(currentObjective) + " deferring chase movement to combat strategy");

                        return true;
                    }

                    if (objectiveTarget)
                    {
                        botAI->rpgInfo.do_quest.lastTrackedTarget = ObjectGuid();
                        botAI->rpgInfo.do_quest.lastTrackedDistance = FLT_MAX;
                        botAI->rpgInfo.do_quest.stagnantTicks = 0;

                        if (!botAI->rpgInfo.do_quest.lastReachPOI)
                            botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();

                        context->GetValue<Unit*>("grind target")->Set(objectiveTarget);

                        if (!bot->IsInCombat())
                        {
                            context->GetValue<Unit*>("current target")->Set(nullptr);
                            TellDoQuestDebug(botAI, bot,
                                             bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                                                 std::to_string(currentObjective) +
                                                 " seeded in-range grind target and yielded for attack-anything flow");
                            return false;
                        }

                        context->GetValue<Unit*>("current target")->Set(objectiveTarget);

                        TellDoQuestDebug(botAI, bot,
                                         bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                                             std::to_string(currentObjective) + " assigned creature entry " +
                                             std::to_string(requiredCreatureEntry) + " as current target");
                        return true;
                    }
                }
                else
                {
                    TellDoQuestDebug(botAI, bot,
                                     bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                                         std::to_string(currentObjective) + " found no matching creature entry " +
                                         std::to_string(requiredCreatureEntry) + " in " +
                                         std::to_string(possibleTargets.size()) + " possible targets");
                }
            }
        }
    }
    else if (currentObjective >= QUEST_OBJECTIVES_COUNT &&
             currentObjective < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
    {
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        uint32 itemObjectiveIndex = currentObjective - QUEST_OBJECTIVES_COUNT;
        if (quest)
        {
            TellDoQuestDebug(botAI, bot,
                             bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                                 std::to_string(currentObjective) + " requires item " +
                                 std::to_string(quest->RequiredItemId[itemObjectiveIndex]) + " count " +
                                 std::to_string(quest->RequiredItemCount[itemObjectiveIndex]) +
                                 ", checking grind target");
        }

        Unit* grindTarget = AI_VALUE(Unit*, "grind target");
        if (!botAI->rpgInfo.do_quest.lastTrackedTarget.IsEmpty())
        {
            Unit* stickyTarget = ObjectAccessor::GetUnit(*bot, botAI->rpgInfo.do_quest.lastTrackedTarget);
            if (stickyTarget && stickyTarget->IsAlive())
            {
                if (!grindTarget || stickyTarget->GetGUID() != grindTarget->GetGUID())
                {
                    TellDoQuestDebug(botAI, bot,
                                     bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                                         std::to_string(currentObjective) + " keeping sticky target entry " +
                                         std::to_string(stickyTarget->GetEntry()) + " at distance " +
                                         std::to_string(bot->GetDistance(stickyTarget)));
                }
                grindTarget = stickyTarget;
            }
        }
        if (grindTarget)
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
                {
                    context->GetValue<Unit*>("current target")->Set(nullptr);

                    if (YieldForNearbyLoot(questId, currentObjective))
                        return true;

                    TellDoQuestDebug(botAI, bot,
                                     bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                                         std::to_string(currentObjective) +
                                         " seeded grind target and yielded for attack-anything flow");
                    return false;
                }

                if (!ShouldIssueQuestChase(botAI->rpgInfo.do_quest, grindTarget->GetGUID(), targetDistance))
                {
                    if (botAI->rpgInfo.do_quest.stagnantTicks <= 1 || botAI->rpgInfo.do_quest.stagnantTicks % 5 == 0)
                    {
                        TellDoQuestDebug(botAI, bot,
                                         bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                                             std::to_string(currentObjective) + " chase throttled for entry " +
                                             std::to_string(grindTarget->GetEntry()) + ", distance " +
                                             std::to_string(targetDistance) + ", stagnantTicks " +
                                             std::to_string(botAI->rpgInfo.do_quest.stagnantTicks));
                    }

                    uint32 stagnant = botAI->rpgInfo.do_quest.stagnantTicks;
                    constexpr uint32 targetReselectTicks = 1800;  // ~3min at 100ms tick
                    constexpr uint32 localRescoutTicks = 2400;    // ~4min at 100ms tick
                    constexpr uint32 rotatePoiTicks = 3000;       // ~5min at 100ms tick

                    if (stagnant >= rotatePoiTicks)
                    {
                        TellDoQuestDebug(botAI, bot,
                                         bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                                             std::to_string(currentObjective) + " extreme stagnation (" +
                                             std::to_string(stagnant) + " ticks, ~5min), clearing POI for rotation");
                        botAI->rpgInfo.do_quest.pos = WorldPosition();
                        botAI->rpgInfo.do_quest.lastReachPOI = 0;
                        botAI->rpgInfo.do_quest.lastTrackedTarget = ObjectGuid();
                        botAI->rpgInfo.do_quest.lastTrackedDistance = FLT_MAX;
                        botAI->rpgInfo.do_quest.stagnantTicks = 0;
                    }
                    else if (stagnant >= localRescoutTicks)
                    {
                        TellDoQuestDebug(botAI, bot,
                                         bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                                             std::to_string(currentObjective) + " sustained stagnation (" +
                                             std::to_string(stagnant) + " ticks, ~4min), forcing local re-scout around POI");
                        botAI->rpgInfo.do_quest.lastTrackedTarget = ObjectGuid();
                        botAI->rpgInfo.do_quest.lastTrackedDistance = FLT_MAX;
                        botAI->rpgInfo.do_quest.stagnantTicks = 0;
                        return MoveRandomNear(35.0f);
                    }
                    else if (stagnant >= targetReselectTicks)
                    {
                        TellDoQuestDebug(botAI, bot,
                                         bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                                             std::to_string(currentObjective) + " stagnation limit (" +
                                             std::to_string(stagnant) + " ticks, ~3min), resetting target tracking");
                        botAI->rpgInfo.do_quest.lastTrackedTarget = ObjectGuid();
                        botAI->rpgInfo.do_quest.lastTrackedDistance = FLT_MAX;
                        botAI->rpgInfo.do_quest.stagnantTicks = 0;
                    }

                    return true;
                }

                TellDoQuestDebug(botAI, bot,
                                 bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                                     std::to_string(currentObjective) +
                                     " deferring chase movement to combat strategy");

                return true;
            }

            if (!botAI->rpgInfo.do_quest.lastReachPOI)
                botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();

            botAI->rpgInfo.do_quest.lastTrackedTarget = ObjectGuid();
            botAI->rpgInfo.do_quest.lastTrackedDistance = FLT_MAX;
            botAI->rpgInfo.do_quest.stagnantTicks = 0;

            if (!bot->IsInCombat())
            {
                context->GetValue<Unit*>("current target")->Set(nullptr);
                TellDoQuestDebug(botAI, bot,
                                 bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                                     std::to_string(currentObjective) +
                                     " seeded in-range grind target and yielded for attack-anything flow");
                return false;
            }

            context->GetValue<Unit*>("current target")->Set(grindTarget);

            TellDoQuestDebug(botAI, bot,
                             bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                                 std::to_string(currentObjective) + " assigned grind target to combat strategy");
            return true;
        }

        if (quest)
        {
            TellDoQuestDebug(botAI, bot,
                             bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                                 std::to_string(currentObjective) + " found no grind target for item " +
                                 std::to_string(quest->RequiredItemId[itemObjectiveIndex]) + " at current POI");
        }

        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " item objective " +
                             std::to_string(currentObjective) + " scouting around POI for quest-drop mobs");
        return MoveRandomNear(35.0f);
    }

    return false;
}

bool NewRpgDoQuestAction::HandleObjectiveStayAndRotation(uint32 questId)
{
    int32 currentObjective = botAI->rpgInfo.do_quest.objectiveIdx;

    if (!botAI->rpgInfo.do_quest.lastReachPOI)
    {
        SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::WaitOrRotateObjective);
        botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " reached POI, start wait/progress window");
        return true;
    }

    SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::WaitOrRotateObjective);

    if (GetMSTimeDiffToNow(botAI->rpgInfo.do_quest.lastReachPOI) >= poiStayTime)
    {
        bool hasProgression = false;
        currentObjective = botAI->rpgInfo.do_quest.objectiveIdx;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);
        if (currentObjective < QUEST_OBJECTIVES_COUNT)
        {
            if (q_status.CreatureOrGOCount[currentObjective] != 0 && quest->RequiredNpcOrGoCount[currentObjective])
                hasProgression = true;
        }
        else if (currentObjective < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
        {
            if (q_status.ItemCount[currentObjective - QUEST_OBJECTIVES_COUNT] != 0 &&
                quest->RequiredItemCount[currentObjective - QUEST_OBJECTIVES_COUNT])
                hasProgression = true;
        }
        if (!hasProgression)
        {
            botAI->lowPriorityQuest.insert(questId);
            botAI->rpgStatistic.questAbandoned++;
            LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {}", bot->GetName(), questId);
            TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) + " timed out at objective " +
                                            std::to_string(currentObjective) +
                                            " with no progression, switch to IDLE");
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

    bool isGameObjectObjective = false;
    if (currentObjective >= 0 && currentObjective < QUEST_OBJECTIVES_COUNT)
    {
        if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
            isGameObjectObjective = quest->RequiredNpcOrGo[currentObjective] < 0;
    }

    if (isGameObjectObjective && bot->GetDistance(botAI->rpgInfo.do_quest.pos) > 25.0f)
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " GO objective drifted from POI, moving back");
        return MoveFarTo(botAI->rpgInfo.do_quest.pos);
    }

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " quest " + std::to_string(questId) + " objective " +
                         std::to_string(currentObjective) + " waiting at POI, random patrol step");
    return MoveRandomNear(20.0f);
}

bool NewRpgDoQuestAction::DoCompletedQuest()
{
    uint32 questId = RPG_INFO(quest, questId);
    const Quest* quest = RPG_INFO(quest, quest);

    if (RPG_INFO(quest, objectiveIdx) != -1)
    {
        if (TryDeferTurnInForNearbyObjective(questId))
            return true;

        if (!SelectRewardPOI(questId, quest))
            return false;
    }

    return HandleRewardPOI(questId);
}

bool NewRpgDoQuestAction::TryDeferTurnInForNearbyObjective(uint32 completedQuestId)
{
    uint32 nearbyQuestId = 0;
    float nearbyDistance = 0.0f;
    if (!FindNearbyIncompleteQuest(completedQuestId, nearbyQuestId, nearbyDistance))
        return false;

    struct DeferredPairState
    {
        uint32 fromQuestId = 0;
        uint32 toQuestId = 0;
        uint32 lastDeferralMs = 0;
    };

    static std::unordered_map<uint64, DeferredPairState> deferredByBot;
    uint64 botGuidRaw = bot->GetGUID().GetRawValue();
    DeferredPairState& state = deferredByBot[botGuidRaw];
    uint32 nowMs = getMSTime();

    if (state.fromQuestId == completedQuestId && state.toQuestId == nearbyQuestId &&
        state.lastDeferralMs && (nowMs - state.lastDeferralMs) < 5000)
    {
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(completedQuestId) +
                             " nearby deferral skipped (repeat guard) to quest " +
                             std::to_string(nearbyQuestId));
        return false;
    }

    Quest const* nearbyQuest = sObjectMgr->GetQuestTemplate(nearbyQuestId);
    if (!nearbyQuest)
        return false;

    state.fromQuestId = completedQuestId;
    state.toQuestId = nearbyQuestId;
    state.lastDeferralMs = nowMs;

    TellDoQuestDebug(botAI, bot,
                     bot->GetName() + " deferring turn-in for completed quest " +
                         std::to_string(completedQuestId) + " to pursue nearby objective quest " +
                         std::to_string(nearbyQuestId) + " at " + std::to_string(nearbyDistance) + " yards");

    botAI->rpgInfo.ChangeToDoQuest(nearbyQuestId, nearbyQuest);
    return true;
}

bool NewRpgDoQuestAction::FindNearbyIncompleteQuest(uint32 completedQuestId, uint32& nearbyQuestId,
                                                    float& nearbyDistance)
{
    static constexpr float nearbyObjectiveRadius = 150.0f;

    nearbyQuestId = 0;
    nearbyDistance = std::numeric_limits<float>::max();

    Map* map = bot->GetMap();
    if (!map)
        return false;

    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (!questId || questId == completedQuestId)
            continue;

        if (botAI->lowPriorityQuest.find(questId) != botAI->lowPriorityQuest.end())
            continue;

        if (bot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            continue;

        std::vector<POIInfo> poiInfo;
        if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo))
            continue;

        for (POIInfo const& poi : poiInfo)
        {
            float x = poi.pos.x;
            float y = poi.pos.y;
            float z = std::max(map->GetHeight(x, y, MAX_HEIGHT), map->GetWaterLevel(x, y));
            if (z == INVALID_HEIGHT || z == VMAP_INVALID_HEIGHT_VALUE)
                continue;

            if (map->GetZoneId(bot->GetPhaseMask(), x, y, z) != bot->GetZoneId())
                continue;

            float distance = bot->GetDistance2d(x, y);
            if (distance > nearbyObjectiveRadius)
                continue;

            if (distance < nearbyDistance)
            {
                nearbyDistance = distance;
                nearbyQuestId = questId;
            }
        }
    }

    return nearbyQuestId != 0;
}

bool NewRpgDoQuestAction::SelectRewardPOI(uint32 questId, Quest const* quest)
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

bool NewRpgDoQuestAction::HandleRewardPOI(uint32 questId)
{
    if (botAI->rpgInfo.do_quest.pos == WorldPosition())
    {
        TellDoQuestDebug(botAI, bot, bot->GetName() + " quest " + std::to_string(questId) + " reward branch has empty POI");
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

    // Now we are near the qoi of reward
    // the quest should be rewarded by SearchQuestGiverAndAcceptOrReward
    if (!botAI->rpgInfo.do_quest.lastReachPOI)
    {
        SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::WaitForTurnIn);
        botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " reached reward POI, waiting for turn-in");
        return true;
    }

    SetDoQuestPhase(botAI, bot, questId, DoQuestPhase::WaitForTurnIn);

    // stayed at this POI for more than 5 minutes
    if (GetMSTimeDiffToNow(botAI->rpgInfo.do_quest.lastReachPOI) >= poiStayTime)
    {
        // e.g. Can not reward quest to gameobjects
        /// @TODO: It may be better to make lowPriorityQuest a global set shared by all bots (or saved in db)
        botAI->lowPriorityQuest.insert(questId);
        botAI->rpgStatistic.questAbandoned++;
        LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {}", bot->GetName(), questId);
        TellDoQuestDebug(botAI, bot,
                         bot->GetName() + " quest " + std::to_string(questId) + " timed out at reward POI, switch to IDLE");
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }
    return false;
}

bool NewRpgTravelFlightAction::Execute(Event event)
{
    if (bot->IsInFlight())
    {
        botAI->rpgInfo.flight.inFlight = true;
        return false;
    }
    Creature* flightMaster = ObjectAccessor::GetCreature(*bot, botAI->rpgInfo.flight.fromFlightMaster);
    if (!flightMaster || !flightMaster->IsAlive())
    {
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }
    const TaxiNodesEntry* entry = sTaxiNodesStore.LookupEntry(botAI->rpgInfo.flight.toNode);
    if (bot->GetDistance(flightMaster) > INTERACTION_DISTANCE)
    {
        return MoveFarTo(flightMaster);
    }
    std::vector<uint32> nodes = {botAI->rpgInfo.flight.fromNode, botAI->rpgInfo.flight.toNode};

    botAI->RemoveShapeshift();
    if (bot->IsMounted())
    {
        bot->Dismount();
    }
    if (!bot->ActivateTaxiPathTo(nodes, flightMaster, 0))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} active taxi path {} (from {} to {}) failed", bot->GetName(),
                  flightMaster->GetEntry(), nodes[0], nodes[1]);
        botAI->rpgInfo.ChangeToIdle();
    }
    return true;
}