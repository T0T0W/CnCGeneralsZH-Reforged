/* Command & Conquer Generals Zero Hour - GPL-3.0-or-later */
#ifndef SELECTION_PRIORITY_H
#define SELECTION_PRIORITY_H

#include "Common/KindOf.h"

// Keep military support alongside combat units, but leave the economy behind.
template <typename Unit>
inline Bool isMilitaryBoxSelectionUnit(const Unit *unit)
{
	return unit && !unit->isKindOf(KINDOF_STRUCTURE) &&
		!unit->isKindOf(KINDOF_DOZER) && !unit->isKindOf(KINDOF_HARVESTER) &&
		!unit->isKindOf(KINDOF_MONEY_HACKER) && !unit->isKindOf(KINDOF_CASH_GENERATOR) &&
		(unit->isKindOf(KINDOF_CAN_ATTACK) || unit->isKindOf(KINDOF_INFANTRY) ||
		 unit->isKindOf(KINDOF_VEHICLE) || unit->isKindOf(KINDOF_AIRCRAFT));
}

// The predicate includes eligibility: an enemy or unselectable soldier must not
// prevent a box containing only our workers from selecting those workers.
template <typename Selection, typename Predicate>
inline void prioritizeMilitaryBoxSelection(Selection& selection, Predicate isMilitary)
{
	typename Selection::iterator it = selection.begin();
	for (; it != selection.end(); ++it)
	{
		if (isMilitary(*it))
			break;
	}
	if (it == selection.end())
		return;

	for (it = selection.begin(); it != selection.end();)
	{
		if (isMilitary(*it))
			++it;
		else
			it = selection.erase(it);
	}
}

#endif
