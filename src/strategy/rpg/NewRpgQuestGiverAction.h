#ifndef _PLAYERBOT_NEWRPGQUESTGIVERACTION_H
#define _PLAYERBOT_NEWRPGQUESTGIVERACTION_H

#include "NewRpgBaseAction.h"

class NewRpgQuestGiverAction : public NewRpgBaseAction
{
public:
    NewRpgQuestGiverAction(PlayerbotAI* botAI) : NewRpgBaseAction(botAI, "new rpg quest giver") {}
    bool Execute(Event event) override;
};

#endif
