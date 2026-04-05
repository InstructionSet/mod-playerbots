/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "HasAvailableLootValue.h"

#include "LootObjectStack.h"
#include "Playerbots.h"

#include <unordered_map>

namespace
{
    bool IsLootDebugEnabled(PlayerbotAI* botAI)
    {
        return botAI &&
               (botAI->HasStrategy("debug loot", BOT_STATE_NON_COMBAT) ||
                botAI->HasStrategy("debug loot", BOT_STATE_COMBAT));
    }
}

bool HasAvailableLootValue::Calculate()
{
    bool canLoot = AI_VALUE(bool, "can loot");
    bool canLootFromStack = AI_VALUE(LootObjectStack*, "available loot")->CanLoot(sPlayerbotAIConfig->lootDistance);
    bool result = !canLoot && canLootFromStack;

    if (IsLootDebugEnabled(botAI))
    {
        static std::unordered_map<uint64, std::string> lastMessageByBot;
        std::string message = "has available loot = " + std::string(result ? "true" : "false") +
                              " (canLoot=" + std::string(canLoot ? "true" : "false") +
                              ", stackHasLoot=" + std::string(canLootFromStack ? "true" : "false") + ")";
        uint64 botGuidRaw = bot->GetGUID().GetRawValue();
        if (!lastMessageByBot.count(botGuidRaw) || lastMessageByBot[botGuidRaw] != message)
        {
            LOG_DEBUG("playerbots", "[LootDebug] {} {}", bot->GetName().c_str(), message.c_str());
            lastMessageByBot[botGuidRaw] = message;
        }
    }

    return result;
}
