/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "BagManagementActions.h"

#include "Playerbots.h"
#include "ServerFacade.h"

bool MoveToSellVendorAction::Execute(Event event)
{
    (void)event;

    Creature* nearestVendor = nullptr;
    float nearestDistance = 0.0f;

    GuidVector npcs = AI_VALUE(GuidVector, "nearest npcs");
    for (ObjectGuid const guid : npcs)
    {
        Creature* vendor = botAI->GetCreature(guid);
        if (!vendor || !vendor->IsInWorld() || !vendor->IsAlive())
            continue;

        if (!(vendor->GetUInt32Value(UNIT_NPC_FLAGS) & UNIT_NPC_FLAG_VENDOR))
            continue;

        if (vendor->GetMapId() != bot->GetMapId())
            continue;

        if (bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_VENDOR))
            continue;

        float distance = sServerFacade->GetDistance2d(bot, vendor);
        if (distance > sPlayerbotAIConfig->sightDistance)
            continue;

        if (!nearestVendor || distance < nearestDistance)
        {
            nearestVendor = vendor;
            nearestDistance = distance;
        }
    }

    if (!nearestVendor)
        return false;

    return MoveTo(nearestVendor, INTERACTION_DISTANCE * 0.9f);
}

bool MoveToSellVendorAction::isUseful()
{
    if (bot->GetGroup())
        return false;

    if (!AI_VALUE(bool, "can move around"))
        return false;

    if (!AI_VALUE(bool, "should sell"))
        return false;

    if (!AI_VALUE2(uint32, "item count", "gray"))
        return false;

    return true;
}
