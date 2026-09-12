#include "WeaponExtDiag.h"

#include <Utilities/Debug.h>

namespace
{
	constexpr int KillLogLimit = 400;
	int KillCount = 0;
	bool BudgetAnnounced = false;

	bool RanExeRun = false;
	bool RanExeRunAlt = false;

	bool Reported = false;
}

void WeaponDiag::MarkExeRun() { RanExeRun = true; }
void WeaponDiag::MarkExeRunAlt() { RanExeRunAlt = true; }

void WeaponDiag::ReportOnce()
{
	if (Reported)
		return;

	Reported = true;

	Debug::Log("[WeaponExt] P0 -- read-only bounty probe at 0x702E64. "
		"No behaviour is modified in this build.\n");
	Debug::Log("[WeaponExt]   init hooks: ExeRun(0x7CD810, 5 rivals)=%s  "
		"ExeRunAlt(0x7CD81E, uncontested)=%s\n",
		RanExeRun ? "RAN" : "DID NOT RUN",
		RanExeRunAlt ? "RAN" : "DID NOT RUN");
	Debug::Log("[WeaponExt]   kill-log budget: %d events.\n", KillLogLimit);
}

bool WeaponDiag::ShouldLog()
{
	if (KillCount >= KillLogLimit)
	{
		if (!BudgetAnnounced)
		{
			BudgetAnnounced = true;
			Debug::Log("[WeaponExt] Kill-log budget reached (%d).\n", KillLogLimit);
		}
		return false;
	}
	++KillCount;
	return true;
}
