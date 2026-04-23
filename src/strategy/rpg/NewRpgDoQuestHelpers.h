#ifndef _PLAYERBOT_NEWRPGDOQUESTHELPERS_H
#define _PLAYERBOT_NEWRPGDOQUESTHELPERS_H

#include <string>

#include "NewRpgInfo.h"
#include "ObjectGuid.h"

class PlayerbotAI;
class Player;

namespace NewRpgDoQuestHelpers
{
    bool IsDoQuestDebugEnabled(PlayerbotAI* botAI);
    void TellDoQuestDebug(PlayerbotAI* botAI, Player* bot, std::string const& message);
    const char* DoQuestPhaseName(DoQuestPhase phase);
    void SetDoQuestPhase(PlayerbotAI* botAI, Player* bot, uint32 questId, DoQuestPhase newPhase);
    bool ShouldIssueQuestChase(NewRpgInfo::DoQuest& doQuest, ObjectGuid targetGuid, float targetDistance,
                               uint32 minIntervalMs = 700, float progressEpsilon = 0.75f);
}  // namespace NewRpgDoQuestHelpers

#endif
