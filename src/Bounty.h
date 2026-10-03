#pragma once

// Bounty payout engine (DESIGN.md section 1, phases B1 + B2).
//
// One primitive: a kill produces an event (killer, victim) and a base amount
// derived from the victim; rules then decide who receives, who pays, and how
// much. Three independent payout routes run off one event:
//
//   1. direct   -- the killer's own type opts in with Bounty.Hunter=yes
//   2. death    -- the victim's type pays its owner (or others) for dying
//   3. leech    -- any live techno with Bounty.Leech=yes skims kills it did
//                  not make, inside Bounty.Leech.Range (-1 = whole map)
//
// DETERMINISM: this engine draws no random numbers at all. Every input is
// synced sim state (types, houses, coordinates) and iteration order over
// TechnoClass::Array / HouseClass::Array is creation order, which is itself
// synced. Ratios are applied with one truncating helper so the arithmetic is
// identical on every client. Nothing here touches ScenarioClass::Random, so
// the ScatterExt zero-draw guarantee is unaffected.

#include <vector>

#include <TechnoTypeClass.h>
#include <HouseClass.h>
#include <Utilities/Enum.h>

class TechnoClass;
class CCINIClass;

// A mixed type list, per Rex's spec: class keywords expand, literal IDs are
// looked up. `Bounty.Victims=VehicleTypes,E1,MTNK,YACNST,BuildingTypes`
//
// An absent or empty key means "everything" -- that is the documented default
// and the common case. `all`/`any` are accepted as explicit synonyms because
// writing them out is the obvious thing to try, and silently treating `all` as
// an unknown TechnoType ID would mean "nothing matches", i.e. a feature that
// reads as broken. `none` is the explicit opposite.
class BountyTypeFilter
{
public:
	void Read(CCINIClass* pINI, const char* pSection, const char* pKey);

	bool Matches(TechnoTypeClass* pType) const
	{
		if (this->IsClosed)
			return false;
		if (this->IsOpen)
			return true;
		if (!pType)
			return false;

		switch (pType->WhatAmI())
		{
		case AbstractType::InfantryType:
			if (this->ClassMask & Class_Infantry) return true;
			break;
		case AbstractType::UnitType:
			if (this->ClassMask & Class_Unit) return true;
			break;
		case AbstractType::BuildingType:
			if (this->ClassMask & Class_Building) return true;
			break;
		case AbstractType::AircraftType:
			if (this->ClassMask & Class_Aircraft) return true;
			break;
		default:
			break;
		}

		for (auto const pListed : this->Types)
		{
			if (pListed == pType)
				return true;
		}

		return false;
	}

	template <typename T>
	bool Serialize(T& Stm)
	{
		return Stm
			.Process(this->IsOpen)
			.Process(this->IsClosed)
			.Process(this->ClassMask)
			.Process(this->Types)
			.Success();
	}

private:
	enum ClassBit : int
	{
		Class_Infantry = 0x1,
		Class_Unit     = 0x2,
		Class_Building = 0x4,
		Class_Aircraft = 0x8,
	};

	bool IsOpen = true;      // absent / empty / "all"
	bool IsClosed = false;   // "none"
	int ClassMask = 0;
	std::vector<TechnoTypeClass*> Types;
};

enum class BountyReceiver : int
{
	Killer = 0,      // default: the house that made the kill
	VictimHouse,     // pay the house you just shot -- deliberately possible
	KillerAllies,    // split-free: every ally of the killer gets the amount
	None,
};

enum class BountyPayer : int
{
	None = 0,        // default: money is created, as vanilla bounty does
	Killer,
	Victim,
};

namespace Bounty
{
	// Ratio application, in exactly one place so every payout rounds the same
	// way on every client. Truncates toward zero.
	inline int Scale(int base, double ratio)
	{
		return static_cast<int>(base * ratio);
	}

	// Guarded house-relation test. Phobos's EnumFunctions::CanTargetHouse is
	// the same logic but dereferences ownerHouse without a null check; a kill
	// event can legitimately carry a null house, so this version checks.
	//
	// NOTE: a non-allied house is "Enemies" here, which includes the Neutral /
	// civilian house. The P0 probe confirmed Neutral-owned kills really do
	// reach this funnel, so `...Houses=enemies` does pay for civilian kills.
	bool HouseAllowed(AffectedHouse flags, HouseClass* pFrom, HouseClass* pTo);

	// Mod-wide fallbacks, read from [General]. Without these, every payout
	// route would need `Bounty.Value=` or a ratio written onto every single
	// victim section before anything paid out at all -- which reads as "the
	// feature is broken" rather than "the feature is unconfigured". A type's
	// own value always wins; these only fill in what it leaves unset.
	//
	//   [General]
	//   Bounty.Value=0          ; flat default for every TechnoType
	//   Bounty.CostRatio=0.0    ; default ratio of the victim's Cost=
	//   Bounty.SoylentRatio=0.0 ; default ratio of the victim's Soylent=
	//
	// Read from the rules INI during type parsing, so no extra hook is needed.
	void ReadDefaults(CCINIClass* pINI);
	int DefaultValue();
	double DefaultCostRatio();
	double DefaultSoylentRatio();

	// `[General] Bounty.Display=yes` (the default) renders the familiar
	// "+$25" flying text over the victim, shown to whichever house earned the
	// money. Without it a working payout is invisible, which reads exactly
	// like a broken one -- that is how this shipped the first time.
	bool DisplayEnabled();

	// Maintains the "does any type use leeching" fast path. Called with the
	// old and new value whenever a type's Bounty.Leech is (re)assigned, so it
	// stays correct across the multi-pass destructive INI reparse documented
	// in the Hook Encyclopedia's Techno-Type-Lifecycle page.
	void NoteLeechChange(bool oldValue, bool newValue);
	bool AnyLeechTypes();

	// The single entry point, called from the RegisterDestruction funnel.
	void OnKill(TechnoClass* pKiller, TechnoClass* pVictim);
}
