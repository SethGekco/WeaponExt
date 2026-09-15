#pragma once

#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>

#include <TechnoTypeClass.h>

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

		ExtData(TechnoTypeClass* OwnerObject) : Extension<TechnoTypeClass>(OwnerObject)
			, InaccuracyModifier { }
			, InaccuracyModifier_Veteran { }
			, InaccuracyModifier_Elite { }
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
