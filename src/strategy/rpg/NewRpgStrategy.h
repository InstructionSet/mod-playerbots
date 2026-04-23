/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_NEWRPGSTRATEGY_H
#define _PLAYERBOT_NEWRPGSTRATEGY_H

#include "Strategy.h"
#include "TravelMgr.h"
#include "NewRpgInfo.h"

class PlayerbotAI;

namespace NewRpgPriority
{
    inline constexpr float StatusUpdate = 11.0f;

    // These status actions are mutually exclusive because only one RPG state trigger should fire at a time.
    inline constexpr float StatusAction = 3.0f;

    // Generic grinding occupies 4.0-4.2:
    // attack anything = 4.0, food = 4.1, drink = 4.2.
    // Keep quest actions just above attack fallback, but below food/drink so recovery wins.
    inline constexpr float QuestDoExecution = 4.01f;
    inline constexpr float QuestDoPositioning = 4.03f;
    inline constexpr float QuestDoLifecycle = 4.05f;
    inline constexpr float QuestDoCompleted = 4.01f;
    inline constexpr float QuestGiver = 4.07f;
    inline constexpr float QuestDeferral = 4.09f;
}

class NewRpgStrategy : public Strategy
{
public:
    NewRpgStrategy(PlayerbotAI* botAI);

    std::string const getName() override { return "new rpg"; }
    NextAction** getDefaultActions() override;
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

#endif
