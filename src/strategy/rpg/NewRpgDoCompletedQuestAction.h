#ifndef _PLAYERBOT_NEWRPGDOCOMPLETEDQUESTACTION_H
#define _PLAYERBOT_NEWRPGDOCOMPLETEDQUESTACTION_H

#include "NewRpgBaseAction.h"

class NewRpgDoCompletedQuestAction : public NewRpgBaseAction
{
public:
    NewRpgDoCompletedQuestAction(PlayerbotAI* botAI) : NewRpgBaseAction(botAI, "new rpg do complete quest") {}
    bool Execute(Event event) override;

protected:
    bool EvaluateCompletedRewardPOISelection(uint32 questId, Quest const* quest, bool& stageResult);
    bool EvaluateCompletedRewardTurnIn(uint32 questId, bool& stageResult);

    bool SelectRewardPOI(uint32 questId, Quest const* quest);
    bool HandleRewardPOI(uint32 questId);
};

#endif
