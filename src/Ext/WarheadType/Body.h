#pragma once

#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>

#include <WarheadTypeClass.h>

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

		ExtData(WarheadTypeClass* OwnerObject) : Extension<WarheadTypeClass>(OwnerObject)
			, WarheadSize_Exempt { false }
			, WarheadSize_IgnoreSpreadBelow { }
			, WarheadSize_IgnoreSpreadAbove { }
			, WarheadSize_MultiplierCap { }
			, WarheadSize_MultiplierFloor { }
			, WarheadSize_SpreadCap { }
			, WarheadSize_SpreadFloor { }
			, WarheadSize_FromZero { 0.0 }
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
				|| WarheadSize_FromZero > 0.0;
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
