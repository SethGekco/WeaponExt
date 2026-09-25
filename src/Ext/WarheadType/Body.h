#pragma once

#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>

#include <WarheadTypeClass.h>
// Complete type needed: ValueableVector<AnimTypeClass*> parsing calls Find().
#include <AnimTypeClass.h>

// House-relation bitmask for WarheadSize.Attach.Houses, relative to the
// firing house.
enum WarheadSizeHouses
{
	WarheadSizeHouse_Owner = 0x1,
	WarheadSizeHouse_Allies = 0x2,
	WarheadSizeHouse_Enemies = 0x4,
	WarheadSizeHouse_All = 0x7,
};

// Warhead-level extension: per-warhead rules for the size multiplier
// (DESIGN.md section 9).
//
// unordered_map mode (Canary, no ExtPointerOffset) -- no pointer slot claimed.
class WarheadTypeExt
{
public:
	using base_type = WarheadTypeClass;

	static constexpr DWORD Canary = 0x5CA77E03;

	class ExtData final : public Extension<WarheadTypeClass>
	{
	public:
		// Never scaled, whatever the country or effect says. For nukes,
		// superweapon warheads, rad sites -- anything tuned to an exact radius.
		Valueable<bool> WarheadSize_Exempt;

		// Per-warhead overrides of the [CombatDamage] limits (section 9.1).
		// Nullable: unset falls through to the global, and an unset global is
		// no limit at all.
		Nullable<double> WarheadSize_IgnoreSpreadBelow;
		Nullable<double> WarheadSize_IgnoreSpreadAbove;
		Nullable<double> WarheadSize_MultiplierCap;
		Nullable<double> WarheadSize_MultiplierFloor;
		Nullable<double> WarheadSize_SpreadCap;
		Nullable<double> WarheadSize_SpreadFloor;

		// CellSpread=0 times anything is still 0. When this is > 0, a
		// CellSpread=0 warhead is treated as having this spread before
		// filtering and scaling. 0 (default) keeps it at 0, which matters
		// because Phobos warhead effects only apply with CellSpread!=0.
		Valueable<double> WarheadSize_FromZero;

		// ---- Step 3: beyond the engine's spread limit (9.4) ----
		// When the scaled spread is larger than the engine's area-damage
		// routine can safely take, CellSpread is held at that limit and the
		// ring beyond it is damaged by our own pass. no = just stop at the
		// limit. Unset falls through to [CombatDamage], then to yes.
		Nullable<bool> WarheadSize_Overflow;

		// ---- Step 2: timed multiplier applied by this warhead (9.1) ----
		// Unset = this warhead attaches nothing. Every techno inside this
		// warhead's (possibly scaled) CellSpread, filtered by Houses, gets the
		// multiplier for Duration frames. Re-applying replaces the value and
		// restarts the timer; it does not stack.
		Nullable<double> WarheadSize_Attach;
		Valueable<int> WarheadSize_Attach_Duration;
		Valueable<int> WarheadSize_Attach_Houses;   // WarheadSizeHouses bitmask

		// ---- Step 2: bigger explosion art (9.5) ----
		// Parallel to the warhead's own AnimList: when the detonation was
		// scaled at or above Threshold, whichever AnimList entry the engine
		// picked is replaced by the entry at the same position here (or the
		// last one if this list is shorter). Unset Threshold = any enlargement.
		ValueableVector<AnimTypeClass*> WarheadSize_AnimList_Scaled;
		Nullable<double> WarheadSize_AnimList_Threshold;

		ExtData(WarheadTypeClass* OwnerObject) : Extension<WarheadTypeClass>(OwnerObject)
			, WarheadSize_Exempt { false }
			, WarheadSize_IgnoreSpreadBelow { }
			, WarheadSize_IgnoreSpreadAbove { }
			, WarheadSize_MultiplierCap { }
			, WarheadSize_MultiplierFloor { }
			, WarheadSize_SpreadCap { }
			, WarheadSize_SpreadFloor { }
			, WarheadSize_FromZero { 0.0 }
			, WarheadSize_Overflow { }
			, WarheadSize_Attach { }
			, WarheadSize_Attach_Duration { 0 }
			, WarheadSize_Attach_Houses { WarheadSizeHouse_All }
			, WarheadSize_AnimList_Scaled { }
			, WarheadSize_AnimList_Threshold { }
		{ }

		virtual ~ExtData() = default;

		virtual void LoadFromINIFile(CCINIClass* pINI) override;
		virtual void Initialize() override { }
		virtual void InvalidatePointer(void* ptr, bool bRemoved) override { }

		virtual void LoadFromStream(PhobosStreamReader& Stm) override;
		virtual void SaveToStream(PhobosStreamWriter& Stm) override;

		bool HasAnyWarheadSize() const
		{
			return WarheadSize_Exempt
				|| WarheadSize_IgnoreSpreadBelow.isset() || WarheadSize_IgnoreSpreadAbove.isset()
				|| WarheadSize_MultiplierCap.isset() || WarheadSize_MultiplierFloor.isset()
				|| WarheadSize_SpreadCap.isset() || WarheadSize_SpreadFloor.isset()
				|| WarheadSize_FromZero > 0.0
				|| WarheadSize_Overflow.isset()
				|| HasAttach() || !WarheadSize_AnimList_Scaled.empty();
		}

		bool HasAttach() const
		{
			return WarheadSize_Attach.isset() && WarheadSize_Attach_Duration > 0;
		}

	private:
		template <typename T>
		void Serialize(T& Stm);
	};

	class ExtContainer final : public Container<WarheadTypeExt>
	{
	public:
		ExtContainer();
		~ExtContainer();
	};

	static ExtContainer ExtMap;
};
