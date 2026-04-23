#ifndef _PLAYERBOT_NEWRPGQUESTLIFECYCLEACTION_H
#define _PLAYERBOT_NEWRPGQUESTLIFECYCLEACTION_H

#include "NewRpgDoIncompleteQuestAction.h"

class NewRpgQuestLifecycleAction : public NewRpgDoIncompleteQuestAction
{
public:
    NewRpgQuestLifecycleAction(PlayerbotAI* botAI)
        : NewRpgDoIncompleteQuestAction(botAI, "new rpg quest lifecycle")
    {
    }

    bool Execute(Event event) override;
};

#endif
