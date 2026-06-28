/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "LootTriggers.h"

#include "LootObjectStack.h"
#include "Playerbots.h"

bool LootAvailableTrigger::IsActive()
{
    // Stay strategy blocks movement, so looting is only possible if already in range.
    // FarFromCurrentLootTrigger handles walking to loot; CanLootTrigger handles the
    // final open check with its own distance gate, so no distance check needed here.
    if (botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT))
        return false;

    return AI_VALUE(bool, "has available loot");
}

bool FarFromCurrentLootTrigger::IsActive()
{
    LootObject loot = AI_VALUE(LootObject, "loot target");
    if (!loot.IsLootPossible(bot))
        return false;

    return AI_VALUE2(float, "distance", "loot target") >= INTERACTION_DISTANCE - 2.0f;
}

bool CanLootTrigger::IsActive() { return AI_VALUE(bool, "can loot"); }
