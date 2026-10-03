#pragma once

#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>

#include <TechnoTypeClass.h>

#include "../../Bounty.h"

// Per-unit accuracy modifiers (DESIGN.md section 18).
//
// Separate from WeaponTypeExt because the same weapon fires from units of
// differing steadiness, and because rank belongs to the unit, not the gun.
class TechnoTypeExt
{
public:
	using base_type = TechnoTypeClass;

	static constexpr DWORD Canary = 0x5CA77E02;

	class ExtData final : public Extension<TechnoTypeClass>
	{
	public:
		// Nullable so "unset" is distinguishable from "set to 1.0", and so the
		// rank variants can fall back to the base value rather than to 1.0.
		Nullable<double> InaccuracyModifier;
		Nullable<double> InaccuracyModifier_Veteran;
		Nullable<double> InaccuracyModifier_Elite;

		// --- Bounty: victim side (what this type is worth when it dies) ---
		Nullable<int> Bounty_Value;
		Nullable<int> Bounty_Value_Veteran;
		Nullable<int> Bounty_Value_Elite;
		Nullable<double> Bounty_CostRatio;
		Nullable<double> Bounty_SoylentRatio;
		Valueable<int> Bounty_DeathReward;
		Valueable<AffectedHouse> Bounty_DeathReward_Houses;

		// --- Bounty: killer side (opt-in; deliberately NOT Antares' Bounty=) ---
		Valueable<bool> Bounty_Hunter;
		Valueable<double> Bounty_Ratio;
		BountyTypeFilter Bounty_Victims;
		Valueable<AffectedHouse> Bounty_VictimHouses;
		BountyReceiver Bounty_Receiver;
		BountyPayer Bounty_Payer;

		// --- Bounty: leeching (earn from kills this object did not make) ---
		Valueable<bool> Bounty_Leech;
		Valueable<double> Bounty_Leech_Range;   // cells; -1 = whole map
		Valueable<double> Bounty_Leech_Ratio;
		BountyTypeFilter Bounty_Leech_Killers;
		Valueable<AffectedHouse> Bounty_Leech_KillerHouses;
		BountyTypeFilter Bounty_Leech_Victims;
		Valueable<AffectedHouse> Bounty_Leech_VictimHouses;

		ExtData(TechnoTypeClass* OwnerObject) : Extension<TechnoTypeClass>(OwnerObject)
			, InaccuracyModifier { }
			, InaccuracyModifier_Veteran { }
			, InaccuracyModifier_Elite { }
			, Bounty_Value { }
			, Bounty_Value_Veteran { }
			, Bounty_Value_Elite { }
			, Bounty_CostRatio { }
			, Bounty_SoylentRatio { }
			, Bounty_DeathReward { 0 }
			, Bounty_DeathReward_Houses { AffectedHouse::Owner }
			, Bounty_Hunter { false }
			, Bounty_Ratio { 1.0 }
			, Bounty_Victims { }
			, Bounty_VictimHouses { AffectedHouse::Enemies }
			, Bounty_Receiver { BountyReceiver::Killer }
			, Bounty_Payer { BountyPayer::None }
			, Bounty_Leech { false }
			, Bounty_Leech_Range { -1.0 }
			, Bounty_Leech_Ratio { 1.0 }
			, Bounty_Leech_Killers { }
			, Bounty_Leech_KillerHouses { AffectedHouse::All }
			, Bounty_Leech_Victims { }
			, Bounty_Leech_VictimHouses { AffectedHouse::Enemies }
		{ }

		virtual ~ExtData() = default;

		virtual void LoadFromINIFile(CCINIClass* pINI) override;
		virtual void Initialize() override { }
		virtual void InvalidatePointer(void* ptr, bool bRemoved) override { }

		virtual void LoadFromStream(PhobosStreamReader& Stm) override;
		virtual void SaveToStream(PhobosStreamWriter& Stm) override;

		bool HasAny() const
		{
			return InaccuracyModifier.isset()
				|| InaccuracyModifier_Veteran.isset()
				|| InaccuracyModifier_Elite.isset();
		}

		// Most specific set value wins: Elite, then Veteran, then the base.
		// A rank variant that is not set falls through rather than defaulting
		// to 1.0, so `InaccuracyModifier=0.8` alone applies at every rank.
		double ForRank(bool isVeteran, bool isElite) const
		{
			if (isElite && InaccuracyModifier_Elite.isset())
				return InaccuracyModifier_Elite;
			if ((isElite || isVeteran) && InaccuracyModifier_Veteran.isset())
				return InaccuracyModifier_Veteran;
			return InaccuracyModifier.Get(1.0);
		}

		// Same fall-through rule as above, for the flat bounty value; an
		// entirely unset type falls back to the mod-wide [General] default.
		int BountyValueForRank(bool isVeteran, bool isElite) const
		{
			if (isElite && Bounty_Value_Elite.isset())
				return Bounty_Value_Elite;
			if ((isElite || isVeteran) && Bounty_Value_Veteran.isset())
				return Bounty_Value_Veteran;
			return Bounty_Value.Get(Bounty::DefaultValue());
		}

		double BountyCostRatio() const
		{
			return Bounty_CostRatio.Get(Bounty::DefaultCostRatio());
		}

		double BountySoylentRatio() const
		{
			return Bounty_SoylentRatio.Get(Bounty::DefaultSoylentRatio());
		}

		bool HasAnyBounty() const
		{
			return Bounty_Value.isset() || Bounty_Value_Veteran.isset()
				|| Bounty_Value_Elite.isset()
				|| Bounty_CostRatio.isset() || Bounty_SoylentRatio.isset()
				|| Bounty_DeathReward != 0
				|| Bounty_Hunter || Bounty_Leech;
		}

	private:
		template <typename T>
		void Serialize(T& Stm);
	};

	class ExtContainer final : public Container<TechnoTypeExt>
	{
	public:
		ExtContainer();
		~ExtContainer();
	};

	static ExtContainer ExtMap;
};
