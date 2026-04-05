/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DebugStrategy.h"

#include "Playerbots.h"

void DebugLootStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
	triggers.push_back(
		new TriggerNode("very often", NextAction::array(0, new NextAction("debug loot status", 1.0f), nullptr)));
}
