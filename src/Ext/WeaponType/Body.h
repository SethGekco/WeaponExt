#pragma once

#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>

#include <WeaponTypeClass.h>

// Complete type required, not just a forward declaration: parsing a
// ValueableVector<TechnoTypeClass*> instantiates TemplateDef's pointer path,
// which calls TechnoTypeClass::Find(). WeaponTypeClass.h pulls only
// AbstractTypeClass.h, so this does not come in for free.
#include <TechnoTypeClass.h>

#include <Curve.h>

// Weapon-level extension: everything keyed on how far away the target is
// (DESIGN.md sections 4, 6, 7, 8).
//
// Container is used in unordered_map mode -- Canary defined, no
// ExtPointerOffset -- so we claim no pointer slot inside WeaponTypeClass and
// cannot collide with Phobos/Antares extension storage.
class WeaponTypeExt
{
public:
	using base_type = WeaponTypeClass;

	// Unique canary, distinct from Phobos's 0x11111111 and the other Ext DLLs'.
	static constexpr DWORD Canary = 0x5CA77E00;
	// No ExtPointerOffset -> Container uses the unordered_map path.

	class ExtData final : public Extension<WeaponTypeClass>
	{
	public:
		// ---- Section 4: RangeScatter ----
		// Region is a pair of cell distances. Unset (max <= min) disables the
		// whole feature, which is why the defaults are deliberately degenerate
		// rather than something like 0,Range -- a mod that sets no tags must
		// behave exactly as vanilla.
		Valueable<double> RangeScatter_RegionMin;
		Valueable<double> RangeScatter_RegionMax;
		Valueable<double> RangeScatter_Increment;
		Valueable<int> RangeScatter_System;         // ScatterSystem
		Valueable<int> RangeScatter_IncrementMode;  // IncrementMode
		Valueable<double> RangeScatter_DirectHitPercentage;

		// Section 21: a user-drawn curve. When set it REPLACES Region +
		// Increment + System, because plot points already carry both the
		// domain and the magnitudes.
		PlotCurve RangeScatter_Curve;

		// ---- Section 6: ROF by range ----
		Valueable<double> ROF_RegionMin;
		Valueable<double> ROF_RegionMax;
		Valueable<double> ROF_Increment;
		Valueable<int> ROF_System;
		Valueable<int> ROF_IncrementMode;
		PlotCurve ROF_Curve;   // value is the ROF multiplier at that range

		// ---- Section 7: auto-fire range window ----
		// Max < 0 means "unset, use the weapon's Range". Resolved at use site,
		// not parse time, because Range may itself be modified by other DLLs.
		Valueable<double> Range_AutoFire_Min;
		Valueable<double> Range_AutoFire_Max;

		// ---- Section 18: InaccuracyModifier ----
		// Static, per-weapon half of the modifier chain. Multiplies with the
		// firing unit's TechnoType modifier and its rank.
		Valueable<double> InaccuracyModifier;

		// ---- Section 8: friendly-fire risk gate ----
		Valueable<bool> FriendlyFire_Check;
		Valueable<double> FriendlyFire_MaxRatio;
		Valueable<int> FriendlyFire_Scope;          // ScatterHouseRelation bitmask
		Valueable<bool> FriendlyFire_IncludeScatter;
		ValueableVector<TechnoTypeClass*> FriendlyFire_Protected;
		Valueable<double> FriendlyFire_Protected_Weight;
		Valueable<double> FriendlyFire_MinTargetValue;

		ExtData(WeaponTypeClass* OwnerObject) : Extension<WeaponTypeClass>(OwnerObject)
			, RangeScatter_RegionMin { 0.0 }
			, RangeScatter_RegionMax { 0.0 }
			, RangeScatter_Increment { 0.0 }
			, RangeScatter_System { (int)ScatterSystem::Line }
			, RangeScatter_IncrementMode { (int)IncrementMode::Peak }
			, RangeScatter_DirectHitPercentage { 0.0 }
			, ROF_RegionMin { 0.0 }
			, ROF_RegionMax { 0.0 }
			, ROF_Increment { 1.0 }
			, ROF_System { (int)ScatterSystem::Line }
			, ROF_IncrementMode { (int)IncrementMode::Peak }
			, Range_AutoFire_Min { 0.0 }
			, Range_AutoFire_Max { -1.0 }
			, InaccuracyModifier { 1.0 }
			, FriendlyFire_Check { false }
			, FriendlyFire_MaxRatio { 0.5 }
			, FriendlyFire_Scope { 0 }
			, FriendlyFire_IncludeScatter { true }
			, FriendlyFire_Protected { }
			, FriendlyFire_Protected_Weight { 10.0 }
			, FriendlyFire_MinTargetValue { 0.0 }
		{ }

		virtual ~ExtData() = default;

		virtual void LoadFromINIFile(CCINIClass* pINI) override;
		virtual void Initialize() override { }
		virtual void InvalidatePointer(void* ptr, bool bRemoved) override { }

		virtual void LoadFromStream(PhobosStreamReader& Stm) override;
		virtual void SaveToStream(PhobosStreamWriter& Stm) override;

		// True when RangeScatter is configured at all, by either route.
		bool HasRangeScatter() const
		{
			return RangeScatter_Curve.IsSet()
				|| (RangeScatter_RegionMax > RangeScatter_RegionMin
					&& RangeScatter_Increment != 0.0);
		}

		bool HasROFCurve() const
		{
			return ROF_Curve.IsSet() || ROF_RegionMax > ROF_RegionMin;
		}

		// Expected scatter radius in cells at this distance. Deterministic --
		// this is the curve value, NOT a sampled draw, so it is safe to use in
		// fire/hold decisions that must agree across clients.
		double ExpectedScatterCells(double distanceCells) const
		{
			if (!HasRangeScatter())
				return 0.0;

			// Plot points win: they are the more specific statement of intent,
			// and they already encode the range dependence that Region +
			// Increment + System would otherwise supply.
			if (RangeScatter_Curve.IsSet())
				return RangeScatter_Curve.Evaluate(distanceCells);

			return Curve::Evaluate(
				(ScatterSystem)RangeScatter_System.Get(),
				(IncrementMode)RangeScatter_IncrementMode.Get(),
				RangeScatter_Increment,
				distanceCells,
				RangeScatter_RegionMin,
				RangeScatter_RegionMax);
		}

	private:
		template <typename T>
		void Serialize(T& Stm);
	};

	class ExtContainer final : public Container<WeaponTypeExt>
	{
	public:
		ExtContainer();
		~ExtContainer();
	};

	static ExtContainer ExtMap;
};

// House-relation bitmask shared by the friendly-fire scope tag.
enum ScatterHouseRelation
{
	ScatterHouse_Owner = 0x1,
	ScatterHouse_Ally = 0x2,
	ScatterHouse_Enemy = 0x4,
	ScatterHouse_Neutral = 0x8,
};
