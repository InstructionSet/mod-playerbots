#ifndef _PLAYERBOT_NEWRPGDOINCOMPLETEQUESTACTION_H
#define _PLAYERBOT_NEWRPGDOINCOMPLETEQUESTACTION_H

#include <string>
#include <vector>

#include "NewRpgBaseAction.h"
#include "NewRpgDoQuestHelpers.h"

class Quest;
class Unit;

class NewRpgDoIncompleteQuestAction : public NewRpgBaseAction
{
public:
    NewRpgDoIncompleteQuestAction(PlayerbotAI* botAI, std::string const& actionName) : NewRpgBaseAction(botAI, actionName)
    {
    }

protected:
    // Tier 1: Lifecycle — PreSync, policy, and recovery
    bool EvaluateObjectivePreSync(uint32 questId);
    bool EvaluateLifecycleRecovery(uint32 questId);

    // Tier 2: Positioning — POI selection and travel
    bool EvaluateObjectivePOISelection(uint32 questId);
    bool EvaluateObjectiveTravel(uint32 questId, int32 currentObjective);

    // Tier 3: Execution — GO/Creature/Item objective attempts
    bool EvaluateGrindYieldGate(uint32 questId, int32 currentObjective, bool& stageResult);

    float GetObjectiveLeashDistance() const;
    bool EnforceObjectiveLeash(uint32 questId, int32 currentObjective);
    void CheckAndClearCompletedObjective(uint32 questId);
    bool SelectIncompleteObjectivePOI(uint32 questId);
    bool ExecuteObjectiveAtPOI(uint32 questId);
    bool HandleGameObjectObjectiveEntries(uint32 questId, int32 currentObjective, uint32 requiredGoEntry,
                                          GuidVector const& gameObjects, std::string const& sourceTag);

    bool FindNearestObjectiveGameObject(uint32 questId, int32 currentObjective, uint32 requiredGoEntry,
                                        GuidVector const& gameObjects, ObjectGuid& nearestGuid,
                                        float& nearestDistance) const;
    bool HandleGameObjectObjective(uint32 questId, int32 currentObjective, uint32 requiredGoEntry);
    std::vector<uint32> FindNearbyGoForRequiredItem(uint32 itemId);
    bool HandleCreatureObjective(uint32 questId, int32 currentObjective, uint32 requiredCreatureEntry);
    bool HandleItemObjective(uint32 questId, int32 currentObjective);
    bool ShouldDeferItemObjectiveUntilPoiArrival(uint32 questId, int32 currentObjective);
    Quest const* LoadItemObjectiveRequirements(uint32 questId, int32 currentObjective, uint32& requiredItemId,
                                               uint32& requiredItemCount);
    bool TryHandleItemObjectiveViaNearbyGos(uint32 questId, int32 currentObjective, uint32 requiredItemId,
                                            bool& shouldContinue);
    bool ShouldSkipItemObjectiveGrindFallbackAtPoi(uint32 questId, int32 currentObjective, uint32 requiredItemId);
    Unit* ResolveItemObjectiveGrindTarget(uint32 questId, int32 currentObjective);
    bool HandleItemObjectiveWithGrindTarget(uint32 questId, int32 currentObjective, Unit* grindTarget);
    void ResetTrackedObjectiveTarget();
    bool YieldTargetToAttackAnythingFlow(uint32 questId, std::string const& objectiveLabel, int32 currentObjective,
                                         Unit* target, bool inRange);
    bool YieldForNearbyLoot(uint32 questId, int32 currentObjective);

private:
    bool HasObjectiveProgress(uint32 questId, int32 currentObjective) const;
};

#endif