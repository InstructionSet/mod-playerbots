/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_BAGMANAGEMENTTRIGGERS_H
#define _PLAYERBOT_BAGMANAGEMENTTRIGGERS_H

#include "Trigger.h"

class PlayerbotAI;

class BagCanSellGrayTrigger : public Trigger
{
public:
    BagCanSellGrayTrigger(PlayerbotAI* botAI, std::string const name = "bag can sell gray", int checkInterval = 2)
        : Trigger(botAI, name, checkInterval)
    {
    }

    bool IsActive() override;
};

class BagMoveToVendorTrigger : public Trigger
{
public:
    BagMoveToVendorTrigger(PlayerbotAI* botAI, std::string const name = "bag move to vendor", int checkInterval = 2)
        : Trigger(botAI, name, checkInterval)
    {
    }

    bool IsActive() override;
};

#endif
