/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "BagManagementTriggers.h"

#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ServerFacade.h"

bool BagCanSellGrayTrigger::IsActive()
{
    if (!AI_VALUE(bool, "can move around"))
        return false;

    if (!AI_VALUE2(uint32, "item count", "gray"))
        return false;

    bool hasVendor = false;
    float nearestVendorDistance = 0.0f;
    GuidVector vendors = AI_VALUE(GuidVector, "nearest npcs");
    for (ObjectGuid const vendorGuid : vendors)
    {
        Creature* vendor = bot->GetNPCIfCanInteractWith(vendorGuid, UNIT_NPC_FLAG_VENDOR);
        if (vendor)
        {
            float distance = sServerFacade->GetDistance2d(bot, vendor);
            if (!hasVendor || distance < nearestVendorDistance)
                nearestVendorDistance = distance;

            hasVendor = true;
        }
    }

    if (!hasVendor)
        return false;

    float maxVendorDistance = bot->GetGroup() ? sPlayerbotAIConfig->farDistance : sPlayerbotAIConfig->sightDistance;
    if (nearestVendorDistance > maxVendorDistance)
        return false;

    return true;
}

bool BagMoveToVendorTrigger::IsActive()
{
    if (bot->GetGroup())
        return false;

    if (!AI_VALUE(bool, "can move around"))
        return false;

    if (!AI_VALUE2(uint32, "item count", "gray"))
        return false;

    GuidVector vendors = AI_VALUE(GuidVector, "nearest npcs");
    for (ObjectGuid const vendorGuid : vendors)
    {
        if (bot->GetNPCIfCanInteractWith(vendorGuid, UNIT_NPC_FLAG_VENDOR))
            return false;

        Creature* vendor = botAI->GetCreature(vendorGuid);
        if (!vendor || !vendor->IsInWorld() || !vendor->IsAlive())
            continue;

        if (!(vendor->GetUInt32Value(UNIT_NPC_FLAGS) & UNIT_NPC_FLAG_VENDOR))
            continue;

        if (vendor->GetMapId() != bot->GetMapId())
            continue;

        if (sServerFacade->GetDistance2d(bot, vendor) <= sPlayerbotAIConfig->sightDistance)
            return true;
    }

    return false;
}
