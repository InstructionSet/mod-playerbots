#ifndef _PLAYERBOT_NEWRPGQUESTPOSITIONINGACTION_H
#define _PLAYERBOT_NEWRPGQUESTPOSITIONINGACTION_H

#include "NewRpgDoIncompleteQuestAction.h"

class NewRpgQuestPositioningAction : public NewRpgDoIncompleteQuestAction
{
public:
    NewRpgQuestPositioningAction(PlayerbotAI* botAI)
        : NewRpgDoIncompleteQuestAction(botAI, "new rpg quest positioning")
    {
    }

    bool Execute(Event event) override;
};

#endif
