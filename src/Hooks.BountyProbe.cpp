#include "WeaponExtDiag.h"

#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <HouseClass.h>

#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

// P0 probe: observe every kill at the RegisterDestruction funnel.
//
// Register facts verified against the reference handler at this exact
// address (Antares Hooks.Bounty.cpp): EDI = killer TechnoClass* (may be
// null), ESI = victim TechnoClass*. Antares' handler returns 0, so the
// Syringe chain reaches us regardless of load order; we also return 0 and
// modify nothing.
//
// The log line captures every axis the B1 payout rules will gate on:
// killer/victim types and houses, alliance, and the two ratio bases
// (Cost, Soylent) plus the victim's death cell for future leech-range work.
DEFINE_HOOK(0x702E64, WeaponExt_RegisterDestruction_Probe, 0x6)
{
	GET(TechnoClass* const, pKiller, EDI);
	GET(TechnoClass* const, pVictim, ESI);

	WeaponDiag::ReportOnce();

	if (WeaponDiag::ShouldLog())
	{
		auto const pVictimType = pVictim->GetTechnoType();
		auto const pVictimHouse = pVictim->Owner;

		const char* killerId = "<none>";
		const char* killerHouse = "<none>";
		bool allied = false;
		bool sameHouse = false;

		if (pKiller)
		{
			killerId = pKiller->GetTechnoType()->ID;
			if (auto const pKillerHouse = pKiller->Owner)
			{
				killerHouse = pKillerHouse->Type->ID;
				sameHouse = pKillerHouse == pVictimHouse;
				allied = !sameHouse && pVictimHouse
					&& pKillerHouse->IsAlliedWith(pVictimHouse);
			}
		}

		Debug::Log("[WeaponExt][kill] %s (%s) killed %s (%s)%s%s "
			"cost=%d soylent=%d at cell (%d,%d)\n",
			killerId, killerHouse,
			pVictimType->ID,
			pVictimHouse ? pVictimHouse->Type->ID : "<none>",
			sameHouse ? " [SAME HOUSE]" : "",
			allied ? " [ALLIED]" : "",
			pVictimType->Cost,
			pVictimType->Soylent,
			pVictim->Location.X / 256, pVictim->Location.Y / 256);
	}

	return 0;
}
