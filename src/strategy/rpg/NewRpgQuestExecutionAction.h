#ifndef _PLAYERBOT_NEWRPGQUESTEXECUTIONACTION_H
#define _PLAYERBOT_NEWRPGQUESTEXECUTIONACTION_H

#include "NewRpgDoIncompleteQuestAction.h"

class NewRpgQuestExecutionAction : public NewRpgDoIncompleteQuestAction
{
public:
    NewRpgQuestExecutionAction(PlayerbotAI* botAI)
        : NewRpgDoIncompleteQuestAction(botAI, "new rpg quest execution")
    {
    }

    bool Execute(Event event) override;
};

#endif
