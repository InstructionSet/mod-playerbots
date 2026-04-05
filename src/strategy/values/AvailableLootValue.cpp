/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "AvailableLootValue.h"

#include "LootObjectStack.h"
#include "Playerbots.h"
#include "ServerFacade.h"

#include <unordered_map>

namespace
{
    bool IsLootDebugEnabled(PlayerbotAI* botAI)
    {
        return botAI &&
               (botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT) ||
                botAI->HasStrategy("debug loot", BOT_STATE_COMBAT));
    }

    void LogCanLootState(PlayerbotAI* botAI, Player* bot, std::string const& message)
    {
        if (!IsLootDebugEnabled(botAI) || !bot)
            return;

        static std::unordered_map<uint64, std::string> lastMessageByBot;
        uint64 botGuidRaw = bot->GetGUID().GetRawValue();
        auto found = lastMessageByBot.find(botGuidRaw);
        if (found != lastMessageByBot.end() && found->second == message)
            return;

        lastMessageByBot[botGuidRaw] = message;
        LOG_DEBUG("playerbots", "[LootDebug] {} {}", bot->GetName().c_str(), message.c_str());
    }
}

AvailableLootValue::AvailableLootValue(PlayerbotAI* botAI, std::string const name)
    : ManualSetValue<LootObjectStack*>(botAI, nullptr, name)
{
    value = new LootObjectStack(botAI->GetBot());
}

AvailableLootValue::~AvailableLootValue() { delete value; }

LootTargetValue::LootTargetValue(PlayerbotAI* botAI, std::string const name)
    : ManualSetValue<LootObject>(botAI, LootObject(), name)
{
}

bool CanLootValue::Calculate()
{
    LootObject loot = AI_VALUE(LootObject, "loot target");
    float distance = AI_VALUE2(float, "distance", "loot target");
    bool result = !loot.IsEmpty() && loot.GetWorldObject(bot) && loot.IsLootPossible(bot) &&
                  sServerFacade->IsDistanceLessOrEqualThan(distance, INTERACTION_DISTANCE - 2);

    if (result)
    {
        LogCanLootState(botAI, bot,
                        "can loot = true for target " + loot.guid.ToString() + " at distance " +
                            std::to_string(distance));
    }
    else
    {
        LogCanLootState(botAI, bot,
                        "can loot = false, lootTarget=" +
                            std::string(loot.IsEmpty() ? "none" : loot.guid.ToString()) +
                            " distance=" + std::to_string(distance));
    }

    return result;
}
