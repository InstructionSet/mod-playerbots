#include "NewRpgQuestDeferralAction.h"

#include <limits>
#include <unordered_map>

#include "IVMapMgr.h"
#include "Map.h"
#include "NewRpgDoQuestHelpers.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "TravelMgr.h"

using namespace NewRpgDoQuestHelpers;

bool NewRpgQuestDeferralAction::Execute(Event /*event*/)
{
    uint32 questId = RPG_INFO(quest, questId);
    uint8 questStatus = bot->GetQuestStatus(questId);

    if (questStatus != QUEST_STATUS_COMPLETE || RPG_INFO(quest, objectiveIdx) == -1)
        return false;

    return TryDeferTurnInForNearbyObjective(questId);
}

bool NewRpgQuestDeferralAction::TryDeferTurnInForNearbyObjective(uint32 completedQuestId)
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

bool NewRpgQuestDeferralAction::FindNearbyIncompleteQuest(uint32 completedQuestId, uint32& nearbyQuestId,
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
