#include "WeaponExtDiag.h"
#include "Bounty.h"

#include <TechnoClass.h>
// Required even though FootClass is never named here: TechnoClass.h pulls in
// Helpers/Cast.h, which instantiates generic_cast<const FootClass*>.
#include <FootClass.h>
#include <TechnoTypeClass.h>
#include <HouseClass.h>

#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

// B1 + B2: pay out bounties at the RegisterDestruction funnel.
//
// Register facts verified against the reference handler at this exact address
// (Antares Hooks.Bounty.cpp) and then RUNTIME-CONFIRMED by the P0 probe in a
// live skirmish: EDI = killer TechnoClass* (may be null), ESI = victim
// TechnoClass*, and the victim is still fully live here (type, Owner and
// Location all readable), which is what makes leech-range logic possible at
// this seat. Antares' handler returns 0, so the Syringe chain reaches us
// regardless of load order; we return 0 too.
//
// No double-pay with Antares: its payout requires its own `Bounty=yes` on the
// killer, ours requires `Bounty.Hunter=yes`. A modder must opt into both to
// get both.
DEFINE_HOOK(0x702E64, WeaponExt_RegisterDestruction_Bounty, 0x6)
{
	GET(TechnoClass* const, pKiller, EDI);
	GET(TechnoClass* const, pVictim, ESI);

	WeaponDiag::ReportOnce();

	auto const pVictimTypeChecked = pVictim ? pVictim->GetTechnoType() : nullptr;

	if (pVictimTypeChecked && WeaponDiag::ShouldLog())
	{
		auto const pVictimType = pVictimTypeChecked;
		auto const pVictimHouse = pVictim->Owner;

		const char* killerId = "<none>";
		const char* killerHouse = "<none>";
		bool allied = false;
		bool sameHouse = false;

		if (auto const pKillerType = pKiller ? pKiller->GetTechnoType() : nullptr)
		{
			killerId = pKillerType->ID;
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

	Bounty::OnKill(pKiller, pVictim);

	return 0;
}
