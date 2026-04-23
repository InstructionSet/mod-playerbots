#include "NewRpgQuestGiverAction.h"

#include "NewRpgDoQuestHelpers.h"
#include "Player.h"
#include "Playerbots.h"

using namespace NewRpgDoQuestHelpers;

bool NewRpgQuestGiverAction::Execute(Event /*event*/)
{
    if (!SearchQuestGiverAndAcceptOrReward())
        return false;

    TellDoQuestDebug(botAI, bot, bot->GetName() + " delayed by questgiver interaction");
    return true;
}
