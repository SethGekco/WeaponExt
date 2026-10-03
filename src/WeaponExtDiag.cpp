#include "WeaponExtDiag.h"

#include <Utilities/Debug.h>

namespace
{
	constexpr int KillLogLimit = 400;
	int KillCount = 0;
	bool BudgetAnnounced = false;

	constexpr int PayoutLogLimit = 600;
	int PayoutCount = 0;
	bool PayoutBudgetAnnounced = false;

	constexpr int MagnetronLogLimit = 200;
	int MagnetronCount = 0;
	bool MagnetronBudgetAnnounced = false;

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

void WeaponDiag::PayoutLine(const char* reason, const char* earner,
	const char* paidHouse, int amount)
{
	if (PayoutCount >= PayoutLogLimit)
	{
		if (!PayoutBudgetAnnounced)
		{
			PayoutBudgetAnnounced = true;
			Debug::Log("[WeaponExt] Payout-log budget reached (%d).\n",
				PayoutLogLimit);
		}
		return;
	}

	++PayoutCount;

	Debug::Log("[WeaponExt][pay %3d] %s: %s -> %s %+d\n",
		PayoutCount, reason ? reason : "?", earner ? earner : "?",
		paidHouse ? paidHouse : "?", amount);
}

void WeaponDiag::MagnetronLine(const char* why, const char* victim,
	const char* firer, bool forcedUnjam)
{
	if (MagnetronCount >= MagnetronLogLimit)
	{
		if (!MagnetronBudgetAnnounced)
		{
			MagnetronBudgetAnnounced = true;
			Debug::Log("[WeaponExt] Magnetron-log budget reached (%d).\n",
				MagnetronLogLimit);
		}
		return;
	}

	++MagnetronCount;

	Debug::Log("[WeaponExt][mag %3d] released %s (held by %s) -- %s%s\n",
		MagnetronCount, victim ? victim : "?", firer ? firer : "?",
		why ? why : "?",
		forcedUnjam ? " [unjammed: cleared IsAttackedByLocomotor]" : "");
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
