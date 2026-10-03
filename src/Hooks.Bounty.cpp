#include "WeaponExtDiag.h"
#include "Bounty.h"

#include <Misc/FlyingStrings.h>

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

// ---------------------------------------------------------------------------
// Render the money strings. Co-hooks TacticalClass::Draw at 0x6D4684, which
// BOTH Antares and Phobos already hook at this same size, each returning 0 --
// so chaining here is the established pattern rather than a gamble.
//
// We need our own call because the FlyingStrings statics compiled into this
// DLL are a separate instance from Phobos.dll's: its UpdateAll drains its
// queue, not ours. Keeping our own copy is the usual co-loaded-ext rule --
// never share mutable state with another framework's DLL.
//
// This is a draw-phase seat on purpose: UpdateAll writes to DSurface::Temp,
// so calling it from a logic-phase hook would draw nothing.
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x6D4684, TacticalClass_Draw_WeaponExtFlyingStrings, 0x6)
{
	FlyingStrings::UpdateAll();

	return 0;
}
