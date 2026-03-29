#include "NewRpgAction.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

#include "BroadcastHelper.h"
#include "ChatHelper.h"
#include "DBCStores.h"
#include "G3D/Vector2.h"
#include "GossipDef.h"
#include "IVMapMgr.h"
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
}

bool TellRpgStatusAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;
    std::string out = botAI->rpgInfo.ToString();
    bot->Whisper(out.c_str(), LANG_UNIVERSAL, owner);
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
        bot->Whisper("Start to do quest " + std::to_string(questId), LANG_UNIVERSAL, owner);
        return true;
    }
    bot->Whisper("Invalid quest " + text, LANG_UNIVERSAL, owner);
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
        bot->Whisper("Usage: new rpg set <idle|rest|go_grind|go_camp|wander_random|wander_npc|do_quest|travel_flight>",
                     LANG_UNIVERSAL, owner);
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
        bot->Whisper("New RPG status set to IDLE", LANG_UNIVERSAL, owner);
        return true;
    }

    if (status == "rest")
    {
        botAI->rpgInfo.ChangeToRest();
        bot->SetStandState(UNIT_STAND_STATE_SIT);
        bot->Whisper("New RPG status set to REST", LANG_UNIVERSAL, owner);
        return true;
    }

    if (status == "wander_random" || status == "wander")
    {
        if (RandomChangeStatus({RPG_WANDER_RANDOM}))
        {
            bot->Whisper("New RPG status set to WANDER_RANDOM", LANG_UNIVERSAL, owner);
            return true;
        }

        bot->Whisper("Unable to set WANDER_RANDOM right now", LANG_UNIVERSAL, owner);
        return false;
    }

    if (status == "wander_npc")
    {
        if (RandomChangeStatus({RPG_WANDER_NPC}))
        {
            bot->Whisper("New RPG status set to WANDER_NPC", LANG_UNIVERSAL, owner);
            return true;
        }

        bot->Whisper("Unable to set WANDER_NPC right now", LANG_UNIVERSAL, owner);
        return false;
    }

    if (status == "go_grind" || status == "grind")
    {
        if (RandomChangeStatus({RPG_GO_GRIND}))
        {
            bot->Whisper("New RPG status set to GO_GRIND", LANG_UNIVERSAL, owner);
            return true;
        }

        bot->Whisper("Unable to set GO_GRIND right now", LANG_UNIVERSAL, owner);
        return false;
    }

    if (status == "go_camp" || status == "camp")
    {
        if (RandomChangeStatus({RPG_GO_CAMP}))
        {
            bot->Whisper("New RPG status set to GO_CAMP", LANG_UNIVERSAL, owner);
            return true;
        }

        bot->Whisper("Unable to set GO_CAMP right now", LANG_UNIVERSAL, owner);
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
                    bot->Whisper("Invalid quest id", LANG_UNIVERSAL, owner);
                    return false;
                }

                if (bot->GetQuestStatus(questId) == QUEST_STATUS_NONE)
                {
                    bot->Whisper("Quest is not in log", LANG_UNIVERSAL, owner);
                    return false;
                }

                botAI->rpgInfo.ChangeToDoQuest(questId, quest);
                bot->Whisper("New RPG status set to DO_QUEST", LANG_UNIVERSAL, owner);
                return true;
            }
        }

        if (RandomChangeStatus({RPG_DO_QUEST}))
        {
            bot->Whisper("New RPG status set to DO_QUEST", LANG_UNIVERSAL, owner);
            return true;
        }

        bot->Whisper("Unable to set DO_QUEST right now", LANG_UNIVERSAL, owner);
        return false;
    }

    if (status == "travel_flight")
    {
        if (RandomChangeStatus({RPG_TRAVEL_FLIGHT}))
        {
            bot->Whisper("New RPG status set to TRAVEL_FLIGHT", LANG_UNIVERSAL, owner);
            return true;
        }

        bot->Whisper("Unable to set TRAVEL_FLIGHT right now", LANG_UNIVERSAL, owner);
        return false;
    }

    bot->Whisper("Unknown RPG status. Try: rpg help", LANG_UNIVERSAL, owner);
    return false;
}

bool HelpRpgStatusAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;

    bot->Whisper("New RPG commands: new rpg status | new rpg set <status> | new rpg reset", LANG_UNIVERSAL, owner);
    bot->Whisper("Compatibility aliases: rpg status | rpg set | rpg reset", LANG_UNIVERSAL, owner);
    bot->Whisper("Statuses: idle, rest, go_grind, go_camp, wander_random, wander_npc, do_quest [questId], travel_flight",
                 LANG_UNIVERSAL, owner);
    return true;
}

bool ResetRpgStatusAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;

    botAI->rpgInfo.ChangeToIdle();
    bot->Whisper("New RPG status reset to IDLE", LANG_UNIVERSAL, owner);
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
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    NewRpgInfo& info = botAI->rpgInfo;
    uint32 questId = RPG_INFO(quest, questId);
    const Quest* quest = RPG_INFO(quest, quest);
    uint8 questStatus = bot->GetQuestStatus(questId);
    switch (questStatus)
    {
        case QUEST_STATUS_INCOMPLETE:
            return DoIncompleteQuest();
        case QUEST_STATUS_COMPLETE:
            return DoCompletedQuest();
        default:
            break;
    }
    botAI->rpgInfo.ChangeToIdle();
    return true;
}

bool NewRpgDoQuestAction::DoIncompleteQuest()
{
    uint32 questId = RPG_INFO(do_quest, questId);
    if (botAI->rpgInfo.do_quest.pos != WorldPosition())
    {
        /// @TODO: extract to a new function
        int32 currentObjective = botAI->rpgInfo.do_quest.objectiveIdx;
        // check if the objective has completed
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
        // the current objective is completed, clear and find a new objective later
        if (completed)
        {
            botAI->rpgInfo.do_quest.lastReachPOI = 0;
            botAI->rpgInfo.do_quest.pos = WorldPosition();
            botAI->rpgInfo.do_quest.objectiveIdx = 0;
        }
    }
    if (botAI->rpgInfo.do_quest.pos == WorldPosition())
    {
        std::vector<POIInfo> poiInfo;
        if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo))
        {
            // can't find a poi pos to go, stop doing quest for now
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        uint32 rndIdx = urand(0, poiInfo.size() - 1);
        G3D::Vector2 nearestPoi = poiInfo[rndIdx].pos;
        int32 objectiveIdx = poiInfo[rndIdx].objectiveIdx;

        float dx = nearestPoi.x, dy = nearestPoi.y;

        // z = MAX_HEIGHT as we do not know accurate z
        float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(dx, dy));

        // double check for GetQuestPOIPosAndObjectiveIdx
        if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
            return false;

        WorldPosition pos(bot->GetMapId(), dx, dy, dz);
        botAI->rpgInfo.do_quest.lastReachPOI = 0;
        botAI->rpgInfo.do_quest.pos = pos;
        botAI->rpgInfo.do_quest.objectiveIdx = objectiveIdx;
    }

    if (bot->GetDistance(botAI->rpgInfo.do_quest.pos) > 10.0f && !botAI->rpgInfo.do_quest.lastReachPOI)
    {
        return MoveFarTo(botAI->rpgInfo.do_quest.pos);
    }
    // Now we are near the quest objective
    // kill mobs and looting quest should be done automatically by grind strategy

    if (!botAI->rpgInfo.do_quest.lastReachPOI)
    {
        botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();
        return true;
    }
    // stayed at this POI for more than 5 minutes
    if (GetMSTimeDiffToNow(botAI->rpgInfo.do_quest.lastReachPOI) >= poiStayTime)
    {
        bool hasProgression = false;
        int32 currentObjective = botAI->rpgInfo.do_quest.objectiveIdx;
        // check if the objective has progression
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
            // we has reach the poi for more than 5 mins but no progession
            // may not be able to complete this quest, marked as abandoned
            /// @TODO: It may be better to make lowPriorityQuest a global set shared by all bots (or saved in db)
            botAI->lowPriorityQuest.insert(questId);
            botAI->rpgStatistic.questAbandoned++;
            LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {}", bot->GetName(), questId);
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        // clear and select another poi later
        botAI->rpgInfo.do_quest.lastReachPOI = 0;
        botAI->rpgInfo.do_quest.pos = WorldPosition();
        botAI->rpgInfo.do_quest.objectiveIdx = 0;
        return true;
    }

    return MoveRandomNear(20.0f);
}

bool NewRpgDoQuestAction::DoCompletedQuest()
{
    uint32 questId = RPG_INFO(quest, questId);
    const Quest* quest = RPG_INFO(quest, quest);

    if (RPG_INFO(quest, objectiveIdx) != -1)
    {
        // if quest is completed, back to poi with -1 idx to reward
        BroadcastHelper::BroadcastQuestUpdateComplete(botAI, bot, quest);
        botAI->rpgStatistic.questCompleted++;
        std::vector<POIInfo> poiInfo;
        if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo, true))
        {
            // can't find a poi pos to reward, stop doing quest for now
            botAI->rpgInfo.ChangeToIdle();
            return false;
        }
        assert(poiInfo.size() > 0);
        // now we get the place to get rewarded
        float dx = poiInfo[0].pos.x, dy = poiInfo[0].pos.y;
        // z = MAX_HEIGHT as we do not know accurate z
        float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(dx, dy));

        // double check for GetQuestPOIPosAndObjectiveIdx
        if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
            return false;

        WorldPosition pos(bot->GetMapId(), dx, dy, dz);
        botAI->rpgInfo.do_quest.lastReachPOI = 0;
        botAI->rpgInfo.do_quest.pos = pos;
        botAI->rpgInfo.do_quest.objectiveIdx = -1;
    }

    if (botAI->rpgInfo.do_quest.pos == WorldPosition())
        return false;

    if (bot->GetDistance(botAI->rpgInfo.do_quest.pos) > 10.0f && !botAI->rpgInfo.do_quest.lastReachPOI)
        return MoveFarTo(botAI->rpgInfo.do_quest.pos);

    // Now we are near the qoi of reward
    // the quest should be rewarded by SearchQuestGiverAndAcceptOrReward
    if (!botAI->rpgInfo.do_quest.lastReachPOI)
    {
        botAI->rpgInfo.do_quest.lastReachPOI = getMSTime();
        return true;
    }
    // stayed at this POI for more than 5 minutes
    if (GetMSTimeDiffToNow(botAI->rpgInfo.do_quest.lastReachPOI) >= poiStayTime)
    {
        // e.g. Can not reward quest to gameobjects
        /// @TODO: It may be better to make lowPriorityQuest a global set shared by all bots (or saved in db)
        botAI->lowPriorityQuest.insert(questId);
        botAI->rpgStatistic.questAbandoned++;
        LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {}", bot->GetName(), questId);
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