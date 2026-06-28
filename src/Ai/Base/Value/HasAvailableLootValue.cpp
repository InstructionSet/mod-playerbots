/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "HasAvailableLootValue.h"

#include "LootObjectStack.h"
#include "Playerbots.h"

bool HasAvailableLootValue::Calculate()
{
    bool canLoot = AI_VALUE(bool, "can loot");
    bool stackCanLoot = AI_VALUE(LootObjectStack*, "available loot")->CanLoot(sPlayerbotAIConfig.lootDistance);
    bool result = !canLoot && stackCanLoot;

    return result;
}
