#include "ScatterExtDiag.h"

#include <Utilities/Debug.h>

#include <cstring>  // _strnicmp / strncpy_s

namespace
{
	// Separate caps. See ShouldLog() in the header for why a single shared
	// budget failed: rifle fire crowded out everything else.
	constexpr int UnscatteredLimit = 40;
	constexpr int ScatteredLimit = 200;
	constexpr int PerWeaponLimit = 30;   // scattered shots, per weapon
	int UnscatteredCount = 0;
	int ScatteredCount = 0;
	int ProbeCount = 0;
	bool BudgetAnnounced = false;        // latch: this message printed ~200x

	// Small fixed table -- no allocation in a hot hook. Weapon IDs are short
	// and the table only ever holds the handful of weapons that scatter.
	constexpr int MaxWeapons = 24;
	struct WeaponSlot { char id[32]; int count; };
	WeaponSlot Weapons[MaxWeapons];
	int WeaponSlots = 0;

	// Returns false when this weapon has used its per-weapon allowance.
	bool TakeWeaponSlot(const char* name)
	{
		if (!name || !*name)
			return true;   // unnamed: fall back to the global caps only

		for (int i = 0; i < WeaponSlots; ++i)
		{
			if (_strnicmp(Weapons[i].id, name, sizeof(Weapons[i].id) - 1) == 0)
			{
				if (Weapons[i].count >= PerWeaponLimit)
					return false;
				++Weapons[i].count;
				return true;
			}
		}

		if (WeaponSlots >= MaxWeapons)
			return true;   // table full: global caps still apply

		auto& slot = Weapons[WeaponSlots++];
		strncpy_s(slot.id, name, sizeof(slot.id) - 1);
		slot.count = 1;
		return true;
	}

	// Init-site markers. Plain bools, written from hook context only.
	bool RanExeRun = false;
	bool RanExeRunAlt = false;
	bool RanCmdLine = false;

	constexpr int ROFLimit = 40;
	int ROFCount = 0;

	int WeaponParses = 0;
	int BulletParses = 0;

	bool Reported = false;
}

void ScatterDiag::MarkExeRun() { RanExeRun = true; }
void ScatterDiag::MarkExeRunAlt() { RanExeRunAlt = true; }
void ScatterDiag::MarkCmdLine() { RanCmdLine = true; }

void ScatterDiag::CountWeaponParse() { ++WeaponParses; }
void ScatterDiag::CountBulletParse() { ++BulletParses; }

void ScatterDiag::ReportOnce()
{
	if (Reported)
		return;

	Reported = true;

	Debug::Log("[WeaponExt] Phase 0 -- parsing + read-only frame probe. "
		"No behaviour is modified in this build.\n");

	// The point of this line. 0x7CD810 is hooked by five frameworks and
	// 0x52F639 by six; Syringe chains handlers but the first to return a
	// non-zero address wins and the rest never execute. 0x7CD81E is hooked by
	// nobody, so it is the control: if Alt ran and the contested ones did not,
	// chain starvation is confirmed and the uncontested site is the fix.
	Debug::Log("[WeaponExt]   init hooks: ExeRun(0x7CD810, 5 rivals)=%s  "
		"ExeRunAlt(0x7CD81E, uncontested)=%s  CmdLineParse(0x52F639, 6 rivals)=%s\n",
		RanExeRun ? "RAN" : "DID NOT RUN",
		RanExeRunAlt ? "RAN" : "DID NOT RUN",
		RanCmdLine ? "RAN" : "DID NOT RUN");

	// Proves the extension containers are alive. The per-weapon INI echo only
	// fires for weapons that set ScatterExt tags, so without these counters a
	// vanilla ruleset gives no evidence the parse path runs at all.
	Debug::Log("[WeaponExt]   INI parse hooks: WeaponType=%d sections, "
		"BulletType=%d sections\n", WeaponParses, BulletParses);

	Debug::Log("[WeaponExt]   probe budget at 0x6FE8EE: %d scattered + %d unscattered shots.\n",
		ScatteredLimit, UnscatteredLimit);
}

void ScatterDiag::ROFLine(const char* weaponName, double distCells,
	int baseDelay, int finalDelay, double multiplier)
{
	if (ROFCount >= ROFLimit)
		return;

	++ROFCount;

	Debug::Log("[WeaponExt][rof %2d] %s at %.2f cells: delay %d -> %d (x%.3f)%s\n",
		ROFCount, weaponName ? weaponName : "?", distCells,
		baseDelay, finalDelay, multiplier,
		ROFCount == ROFLimit ? "  [ROF budget exhausted]" : "");
}

bool ScatterDiag::ShouldLog(bool interesting, const char* weaponName)
{
	if (interesting)
	{
		if (ScatteredCount >= ScatteredLimit)
			return false;
		if (!TakeWeaponSlot(weaponName))
			return false;
		++ScatteredCount;
		return true;
	}

	if (UnscatteredCount >= UnscatteredLimit)
		return false;
	++UnscatteredCount;
	return true;
}

void ScatterDiag::ProbeLine(const char* firer, const char* target,
	int weaponIndex, const char* weaponName,
	int aimX, int aimY, int aimZ,
	int engX, int engY, int engZ,
	int finX, int finY, int finZ, bool overridden, bool directHit,
	double distanceLeptons)
{
	++ProbeCount;

	// Deltas are printed in both leptons and cells: DESIGN.md claims the aim
	// triple is a firer->target *delta*, so |aim| should track the engagement
	// range in cells. Confirmed in the 2026-09-05 run (199/200 in 0.57-12
	// cells); kept because it is the cheapest possible regression check.
	// Weapon NAME as well as index: Phase 1 has to correlate observed scatter
	// against the ScatterExt tags on a named [Weapon] section, and an index
	// alone cannot do that.
	Debug::Log("[WeaponExt][probe %3d] firer=%s target=%s weapon=%d(%s)\n",
		ProbeCount, firer ? firer : "<null>", target ? target : "<null>",
		weaponIndex, weaponName ? weaponName : "?");
	Debug::Log("[WeaponExt]   aim=(%d,%d,%d) engine=(%d,%d,%d) engDelta=(%d,%d,%d)\n",
		aimX, aimY, aimZ, engX, engY, engZ,
		engX - aimX, engY - aimY, engZ - aimZ);
	Debug::Log("[WeaponExt]   final=(%d,%d,%d) delta=(%d,%d,%d) %s\n",
		finX, finY, finZ,
		finX - aimX, finY - aimY, finZ - aimZ,
		!overridden ? "[engine]"
			: directHit ? "[WEAPONEXT direct-hit]"
			: "[WEAPONEXT]");
	Debug::Log("[WeaponExt]   |aim|=%.1f leptons (%.2f cells)\n",
		distanceLeptons, distanceLeptons / 256.0);

	// Latched. Without this the condition stays true once EITHER counter is
	// pegged, and the line repeats on every subsequent record -- it printed
	// ~200 times in the 2026-09-05 run.
	if (!BudgetAnnounced
		&& ScatteredCount >= ScatteredLimit && UnscatteredCount >= UnscatteredLimit)
	{
		BudgetAnnounced = true;
		Debug::Log("[WeaponExt] Budget reached (scattered %d/%d, unscattered %d/%d).\n",
			ScatteredCount, ScatteredLimit, UnscatteredCount, UnscatteredLimit);
	}
}
