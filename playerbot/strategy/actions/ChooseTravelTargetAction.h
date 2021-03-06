#pragma once

#include "../Action.h"
#include "MovementActions.h"
#include "../../Travelmgr.h"

namespace ai
{
    class ChooseTravelTargetAction : public MovementAction {
    public:
        ChooseTravelTargetAction(PlayerbotAI* ai, string name = "choose travel target") : MovementAction(ai, name) {}

        virtual bool Execute(Event event);
        virtual bool isUseful();

        private:


        TravelTarget ChooseTarget(TravelTarget* oldTarget);

        TravelTarget getBestTarget(vector<TravelDestination*> activeDestinations, vector<WorldPosition*> activePoints, bool groupCopy = false);

        TravelTarget GetGroupTarget();
        TravelTarget GetCurrentTarget(TravelTarget* oldTarget);
        TravelTarget GetQuestTarget();
        TravelTarget GetNewQuestTarget();
        TravelTarget GetNullTarget();

        void ReportTravelTarget(TravelTarget* newTarget, TravelTarget* oldTarget);

        virtual bool needForQuest(Unit* target);
        virtual bool needItemForQuest(uint32 itemId, const Quest* questTemplate, const QuestStatusData* questStatus);
    };
}
