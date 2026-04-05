/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_LOOTDEBUGACTION_H
#define _PLAYERBOT_LOOTDEBUGACTION_H

#include "Action.h"

class LootDebugStatusAction : public Action
{
public:
    LootDebugStatusAction(PlayerbotAI* botAI) : Action(botAI, "debug loot status") {}

    bool Execute(Event event) override;
    bool isUseful() override;
};

#endif
