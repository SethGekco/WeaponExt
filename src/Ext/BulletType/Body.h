#pragma once

#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>

#include <BulletTypeClass.h>

// PlotCurve for the per-axis drawn curves (DESIGN.md section 21).
#include <Curve.h>

// Projectile-level extension: per-axis scatter shape (DESIGN.md section 5).
//
// This lives on BulletTypeClass rather than WeaponTypeClass to sit alongside
// vanilla's BallisticScatter and Antares' BallisticScatter.Min/.Max, which are
// projectile tags. A modder editing scatter should find all of it in one place.
//
// unordered_map mode (Canary, no ExtPointerOffset) -- no pointer slot claimed.
class BulletTypeExt
{
public:
	using base_type = BulletTypeClass;

	static constexpr DWORD Canary = 0x5CA77E01;

	class ExtData final : public Extension<BulletTypeClass>
	{
	public:
		// Per-axis scatter in cells. Nullable so "unset" is distinguishable
		// from "set to zero": unset axes fall back to the existing scalar
		// BallisticScatter.Min/.Max behaviour, which is what makes omitting
		// every tag reproduce vanilla exactly.
		Nullable<double> ScatterMinX, ScatterMaxX;
		Nullable<double> ScatterMinY, ScatterMaxY;
		Nullable<double> ScatterMinZ, ScatterMaxZ;

		// Firer-relative (default) or world axes. See the axis-frame discussion
		// in DESIGN.md section 5 -- world axes make a unit firing north behave
		// differently from the same unit firing east, which is almost never
		// what a modder means by "Y".
		Valueable<bool> AxisFirerRelative;

		// Section 21: per-axis user-drawn curves. When an axis has one, it
		// supplies that axis's Max at the current range, replacing the scalar
		// Max for that axis only -- so X can be a drawn curve while Y stays a
		// flat number.
		PlotCurve MaxCurveX, MaxCurveY, MaxCurveZ;

		ExtData(BulletTypeClass* OwnerObject) : Extension<BulletTypeClass>(OwnerObject)
			, ScatterMinX { }, ScatterMaxX { }
			, ScatterMinY { }, ScatterMaxY { }
			, ScatterMinZ { }, ScatterMaxZ { }
			, AxisFirerRelative { true }
		{ }

		virtual ~ExtData() = default;

		virtual void LoadFromINIFile(CCINIClass* pINI) override;
		virtual void Initialize() override { }
		virtual void InvalidatePointer(void* ptr, bool bRemoved) override { }

		virtual void LoadFromStream(PhobosStreamReader& Stm) override;
		virtual void SaveToStream(PhobosStreamWriter& Stm) override;

		// True when any axis was configured, i.e. when we should take over the
		// aim vector at all. False means "leave vanilla/Antares alone".
		bool HasPerAxis() const
		{
			return ScatterMinX.isset() || ScatterMaxX.isset()
				|| ScatterMinY.isset() || ScatterMaxY.isset()
				|| ScatterMinZ.isset() || ScatterMaxZ.isset()
				|| MaxCurveX.IsSet() || MaxCurveY.IsSet() || MaxCurveZ.IsSet();
		}

		// Max for one axis at this range: the drawn curve if there is one,
		// otherwise the scalar, otherwise the caller's default.
		double MaxXAt(double d, double fallback) const
		{ return MaxCurveX.IsSet() ? MaxCurveX.Evaluate(d) : ScatterMaxX.Get(fallback); }
		double MaxYAt(double d, double fallback) const
		{ return MaxCurveY.IsSet() ? MaxCurveY.Evaluate(d) : ScatterMaxY.Get(fallback); }
		double MaxZAt(double d, double fallback) const
		{ return MaxCurveZ.IsSet() ? MaxCurveZ.Evaluate(d) : ScatterMaxZ.Get(fallback); }

	private:
		template <typename T>
		void Serialize(T& Stm);
	};

	class ExtContainer final : public Container<BulletTypeExt>
	{
	public:
		ExtContainer();
		~ExtContainer();
	};

	static ExtContainer ExtMap;
};
