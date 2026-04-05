/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "NewRpgStrategy.h"

#include "Playerbots.h"

namespace
{
class NewRpgDoQuestPriorityMultiplier : public Multiplier
{
public:
    NewRpgDoQuestPriorityMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "new rpg do quest priority") {}

    float GetValue(Action* action) override
    {
        bool const isAttackAnythingAction = action && action->getName() == "attack anything";
        if (!isAttackAnythingAction)
            return 1.0f;

        NewRpgInfo const& rpg = botAI->rpgInfo;
        bool const isDoQuest = (rpg.status == RPG_DO_QUEST);
        if (!isDoQuest)
            return 1.0f;

        NewRpgInfo::DoQuest const& doQuest = rpg.do_quest;
        bool const hasObjectivePoi = (doQuest.pos != WorldPosition());
        bool const hasReachedPoi = (doQuest.lastReachPOI != 0);
        bool const isStillTravelingToPoi = hasObjectivePoi && !hasReachedPoi &&
                                        botAI->GetBot()->GetDistance(doQuest.pos) > 10.0f;

        return isStillTravelingToPoi ? 0.0f : 1.0f;
    }
};
}

NewRpgStrategy::NewRpgStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

NextAction** NewRpgStrategy::getDefaultActions()
{
    // the releavance should be greater than grind
    return NextAction::array(0,
        new NextAction("new rpg status update", 11.0f),
        nullptr);
}

void NewRpgStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode("go grind status", NextAction::array(0, new NextAction("new rpg go grind", 3.0f), nullptr)));

    triggers.push_back(
        new TriggerNode("go camp status", NextAction::array(0, new NextAction("new rpg go camp", 3.0f), nullptr)));

    triggers.push_back(
        new TriggerNode("wander random status", NextAction::array(0, new NextAction("new rpg wander random", 3.0f), nullptr)));

    triggers.push_back(
        new TriggerNode("wander npc status", NextAction::array(0, new NextAction("new rpg wander npc", 3.0f), nullptr)));

    triggers.push_back(
        new TriggerNode("do quest status", NextAction::array(0, new NextAction("new rpg do quest", 4.1f), nullptr)));

    triggers.push_back(
        new TriggerNode("travel flight status", NextAction::array(0, new NextAction("new rpg travel flight", 3.0f), nullptr)));
}

void NewRpgStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new NewRpgDoQuestPriorityMultiplier(botAI));
}
