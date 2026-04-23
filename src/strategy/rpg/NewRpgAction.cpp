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
#include "Logging/Log.h"
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