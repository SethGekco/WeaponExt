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

		// Clamp on the SCALED CellSpread, in cells. Nullable so an unset bound
		// is no bound at all rather than a bound of zero.
		Nullable<double> WarheadSize_Min;
		Nullable<double> WarheadSize_Max;

		// CellSpread=0 times anything is still 0. When this is > 0, a
		// CellSpread=0 warhead under a multiplier other than 1.0 is treated as
		// having this spread before scaling. 0 (default) keeps it at 0, which
		// matters because Phobos warhead effects only apply with CellSpread!=0.
		Valueable<double> WarheadSize_FromZero;

		ExtData(WarheadTypeClass* OwnerObject) : Extension<WarheadTypeClass>(OwnerObject)
			, WarheadSize_Exempt { false }
			, WarheadSize_Min { }
			, WarheadSize_Max { }
			, WarheadSize_FromZero { 0.0 }
		{ }

		virtual ~ExtData() = default;

		virtual void LoadFromINIFile(CCINIClass* pINI) override;
		virtual void Initialize() override { }
		virtual void InvalidatePointer(void* ptr, bool bRemoved) override { }

		virtual void LoadFromStream(PhobosStreamReader& Stm) override;
		virtual void SaveToStream(PhobosStreamWriter& Stm) override;

		// The CellSpread this warhead should detonate with under `multiplier`.
		// Pure function of INI data + the multiplier, so every client computes
		// the same value -- required, because CellSpread decides who is damaged.
		double ScaledSpread(double original, double multiplier) const
		{
			double base = original;
			if (base <= 0.0)
			{
				if (WarheadSize_FromZero <= 0.0)
					return original;
				base = WarheadSize_FromZero;
			}

			double scaled = base * multiplier;

			if (WarheadSize_Min.isset() && scaled < WarheadSize_Min.Get())
				scaled = WarheadSize_Min.Get();
			if (WarheadSize_Max.isset() && scaled > WarheadSize_Max.Get())
				scaled = WarheadSize_Max.Get();
			if (scaled < 0.0)
				scaled = 0.0;

			return scaled;
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
