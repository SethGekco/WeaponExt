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

	constexpr int GrabLogLimit = 60;
	int GrabCount = 0;
	constexpr int HoldLogLimit = 120;
	int HoldCount_ = 0;

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
	const char* paidHouse, int amount, int houseIndex, bool isCurrentPlayer)
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

	Debug::Log("[WeaponExt][pay %3d] %s: %s -> %s(house %d)%s %+d\n",
		PayoutCount, reason ? reason : "?", earner ? earner : "?",
		paidHouse ? paidHouse : "?", houseIndex,
		isCurrentPlayer ? " [YOU]" : "", amount);
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
		forcedUnjam ? " [restored: ended piggyback + cleared jam bools]" : "");
}

void WeaponDiag::MagnetronGrabLine(const char* firer, const char* victim,
	const char* warhead, const char* outcome)
{
	if (GrabCount >= GrabLogLimit)
		return;

	++GrabCount;

	Debug::Log("[WeaponExt][grab %2d] %s imbued %s via warhead %s -> %s\n",
		GrabCount, firer ? firer : "?", victim ? victim : "?",
		warhead ? warhead : "<none found>", outcome ? outcome : "?");
}

void WeaponDiag::MagnetronHoldLine(const char* victim, int heldFrames,
	int framesSinceImbue, bool jammed, bool sourceMatches)
{
	if (HoldCount_ >= HoldLogLimit)
		return;

	++HoldCount_;

	Debug::Log("[WeaponExt][hold %3d] %s held=%d sinceImbue=%d jammed=%s "
		"sourceMatches=%s\n",
		HoldCount_, victim ? victim : "?", heldFrames, framesSinceImbue,
		jammed ? "yes" : "NO", sourceMatches ? "yes" : "NO");
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
