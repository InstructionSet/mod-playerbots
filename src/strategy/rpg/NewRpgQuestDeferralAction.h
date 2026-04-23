#ifndef _PLAYERBOT_NEWRPGQUESTDEFERRALACTION_H
#define _PLAYERBOT_NEWRPGQUESTDEFERRALACTION_H

#include "NewRpgBaseAction.h"

class NewRpgQuestDeferralAction : public NewRpgBaseAction
{
public:
    NewRpgQuestDeferralAction(PlayerbotAI* botAI) : NewRpgBaseAction(botAI, "new rpg quest deferral") {}
    bool Execute(Event event) override;

private:
    bool TryDeferTurnInForNearbyObjective(uint32 completedQuestId);
    bool FindNearbyIncompleteQuest(uint32 completedQuestId, uint32& nearbyQuestId, float& nearbyDistance);
};

#endif
