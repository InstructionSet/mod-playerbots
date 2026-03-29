/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "BagManagementStrategy.h"

#include "Playerbots.h"

NextAction** BagManagementStrategy::getDefaultActions() { return nullptr; }

void BagManagementStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Solo bots can walk toward a nearby vendor before entering interaction range.
    triggers.push_back(new TriggerNode("bag move to vendor",
                                       NextAction::array(0, new NextAction("move to sell vendor", 1.3f), nullptr)));

    // Sell gray items whenever a vendor is close enough to interact.
    triggers.push_back(
        new TriggerNode("bag can sell gray", NextAction::array(0, new NextAction("sell gray", 1.2f), nullptr)));
}
