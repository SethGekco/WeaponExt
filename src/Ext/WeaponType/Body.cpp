#include "Body.h"

#include <Utilities/Macro.h>
#include <Utilities/Debug.h>

#include <WeaponExt.h>
#include <ScatterExtDiag.h>
#include <CurveParse.h>

// _strcmpi / strtok / strchr / sscanf_s. Windows.h drags most of this in, but
// depending on it transitively is how a build breaks on an unrelated change.
#include <cstring>
#include <cstdio>

WeaponTypeExt::ExtContainer WeaponTypeExt::ExtMap;

WeaponTypeExt::ExtContainer::ExtContainer() : Container<WeaponTypeExt>("WeaponTypeClass") { }
WeaponTypeExt::ExtContainer::~ExtContainer() = default;

namespace
{
	// Small case-insensitive keyword parsers. Phobos's Parser<T> covers the
	// built-in enums but not ours, and hand-rolling keeps the tag values
	// readable in the INI ("Parabola", not "2").
	bool Matches(const char* a, const char* b)
	{
		return _strcmpi(a, b) == 0;
	}

	// Returns the parsed value, or `fallback` when the key is absent.
	// Logs and falls back on an unrecognised value rather than silently
	// treating it as the default -- a typo'd System= is exactly the kind of
	// thing that otherwise costs an hour of confused testing.
	int ReadSystem(CCINIClass* pINI, const char* section, const char* key, int fallback)
	{
		if (!pINI->ReadString(section, key, "", WeaponExtDLL::readBuffer, WeaponExtDLL::readLength))
			return fallback;

		const char* v = WeaponExtDLL::readBuffer;
		if (!*v)
			return fallback;

		if (Matches(v, "Line"))        return (int)ScatterSystem::Line;
		if (Matches(v, "Exponential")) return (int)ScatterSystem::Exponential;
		if (Matches(v, "Parabola"))    return (int)ScatterSystem::Parabola;
		if (Matches(v, "Cubic"))       return (int)ScatterSystem::Cubic;

		Debug::Log("[WeaponExt] %s: unrecognised %s=%s "
			"(expected Line|Exponential|Parabola|Cubic); using default.\n",
			section, key, v);
		return fallback;
	}

	int ReadMode(CCINIClass* pINI, const char* section, const char* key, int fallback)
	{
		if (!pINI->ReadString(section, key, "", WeaponExtDLL::readBuffer, WeaponExtDLL::readLength))
			return fallback;

		const char* v = WeaponExtDLL::readBuffer;
		if (!*v)
			return fallback;

		if (Matches(v, "Peak")) return (int)IncrementMode::Peak;
		if (Matches(v, "Rate")) return (int)IncrementMode::Rate;

		Debug::Log("[WeaponExt] %s: unrecognised %s=%s (expected Peak|Rate); using default.\n",
			section, key, v);
		return fallback;
	}

	// "min,max" pair of cell distances.
	void ReadRegion(CCINIClass* pINI, const char* section, const char* key,
		Valueable<double>& outMin, Valueable<double>& outMax)
	{
		if (!pINI->ReadString(section, key, "", WeaponExtDLL::readBuffer, WeaponExtDLL::readLength))
			return;

		double lo = 0.0, hi = 0.0;
		if (sscanf_s(WeaponExtDLL::readBuffer, "%lf , %lf", &lo, &hi) == 2)
		{
			if (hi <= lo)
			{
				Debug::Log("[WeaponExt] %s: %s=%s has max <= min; feature disabled.\n",
					section, key, WeaponExtDLL::readBuffer);
				return;
			}
			outMin = lo;
			outMax = hi;
		}
		else
		{
			Debug::Log("[WeaponExt] %s: could not parse %s=%s (expected \"min,max\").\n",
				section, key, WeaponExtDLL::readBuffer);
		}
	}

	// Accepts "15", "15%", "0.15" -- percentages are what the design documents,
	// but modders write all three. Values are clamped, per DESIGN.md section 4:
	// negative means never, above 100 means always.
	double ReadPercentage(CCINIClass* pINI, const char* section, const char* key, double fallback)
	{
		if (!pINI->ReadString(section, key, "", WeaponExtDLL::readBuffer, WeaponExtDLL::readLength))
			return fallback;

		const char* v = WeaponExtDLL::readBuffer;
		if (!*v)
			return fallback;

		double parsed = 0.0;
		if (sscanf_s(v, "%lf", &parsed) != 1)
		{
			Debug::Log("[WeaponExt] %s: could not parse %s=%s; using default.\n", section, key, v);
			return fallback;
		}

		// A bare fraction in [0,1] with no '%' is read as a fraction; anything
		// else is a percentage. "1" is therefore 1%, not 100% -- documented,
		// and the ambiguity is why the design writes the tag with a % sign.
		const bool hasPercent = strchr(v, '%') != nullptr;
		if (!hasPercent && parsed > 0.0 && parsed <= 1.0)
			parsed *= 100.0;

		if (parsed < 0.0) parsed = 0.0;
		if (parsed > 100.0) parsed = 100.0;
		return parsed;
	}
}

void WeaponTypeExt::ExtData::LoadFromINIFile(CCINIClass* pINI)
{
	auto pThis = this->OwnerObject();
	const char* section = pThis->ID;

	if (!pINI->GetSection(section))
		return;

	INI_EX exINI(pINI);

	// ---- Section 4: RangeScatter ----
	ReadRegion(pINI, section, "RangeScatter.Region",
		this->RangeScatter_RegionMin, this->RangeScatter_RegionMax);
	this->RangeScatter_Increment.Read(exINI, section, "RangeScatter.Increment");
	this->RangeScatter_System = ReadSystem(pINI, section, "RangeScatter.System",
		this->RangeScatter_System);
	this->RangeScatter_IncrementMode = ReadMode(pINI, section, "RangeScatter.Increment.Mode",
		this->RangeScatter_IncrementMode);
	this->RangeScatter_DirectHitPercentage = ReadPercentage(pINI, section,
		"RangeScatter.DirectHitPercentage", this->RangeScatter_DirectHitPercentage);

	// Section 21 plot points. Parsed AFTER the scalar tags so a warning about
	// an unusable combination can mention both.
	{
		int bad = 0; bool full = false;
		CurveParse::ReadPlotCurve(pINI, section, "RangeScatter",
			this->RangeScatter_Curve, WeaponExtDLL::readBuffer,
			WeaponExtDLL::readLength, &bad, &full);
		if (bad)
			Debug::Log("[WeaponExt] %s: %d malformed RangeScatter.At[...] key(s) ignored.\n",
				section, bad);
		if (full)
			Debug::Log("[WeaponExt] %s: RangeScatter curve hit the %d-point limit; "
				"later points dropped.\n", section, MaxCurvePoints);
		int orphan = 0;
		CurveParse::ReadPlotEasing(pINI, section, "RangeScatter",
			this->RangeScatter_Curve, WeaponExtDLL::readBuffer,
			WeaponExtDLL::readLength, &orphan);
		if (orphan)
			Debug::Log("[WeaponExt] %s: %d RangeScatter.At[...].Ease/.Power key(s) "
				"reference a range with no plot point.\n", section, orphan);

		if (this->RangeScatter_Curve.IsSet()
			&& this->RangeScatter_RegionMax > this->RangeScatter_RegionMin)
		{
			Debug::Log("[WeaponExt] %s: RangeScatter.At[...] plot points override "
				"Region/Increment/System.\n", section);
		}
	}

	// Parabola has no unbounded form; Rate silently degrades to Peak inside
	// Curve::Evaluate, but the modder should hear about it here.
	if (this->RangeScatter_System == (int)ScatterSystem::Parabola
		&& this->RangeScatter_IncrementMode == (int)IncrementMode::Rate)
	{
		Debug::Log("[WeaponExt] %s: RangeScatter.System=Parabola cannot use "
			"Increment.Mode=Rate (a valley has no unbounded form); using Peak.\n", section);
		this->RangeScatter_IncrementMode = (int)IncrementMode::Peak;
	}

	// ---- Section 6: ROF by range ----
	ReadRegion(pINI, section, "ROF.Region", this->ROF_RegionMin, this->ROF_RegionMax);
	this->ROF_Increment.Read(exINI, section, "ROF.Increment");
	this->ROF_System = ReadSystem(pINI, section, "ROF.System", this->ROF_System);
	this->ROF_IncrementMode = ReadMode(pINI, section, "ROF.Increment.Mode", this->ROF_IncrementMode);

	if (this->ROF_System == (int)ScatterSystem::Parabola
		&& this->ROF_IncrementMode == (int)IncrementMode::Rate)
	{
		Debug::Log("[WeaponExt] %s: ROF.System=Parabola cannot use "
			"Increment.Mode=Rate; using Peak.\n", section);
		this->ROF_IncrementMode = (int)IncrementMode::Peak;
	}

	{
		int bad = 0; bool full = false;
		CurveParse::ReadPlotCurve(pINI, section, "ROF",
			this->ROF_Curve, WeaponExtDLL::readBuffer,
			WeaponExtDLL::readLength, &bad, &full);
		if (bad)
			Debug::Log("[WeaponExt] %s: %d malformed ROF.At[...] key(s) ignored.\n",
				section, bad);
		if (full)
			Debug::Log("[WeaponExt] %s: ROF curve hit the %d-point limit.\n",
				section, MaxCurvePoints);
		int orphan = 0;
		CurveParse::ReadPlotEasing(pINI, section, "ROF", this->ROF_Curve,
			WeaponExtDLL::readBuffer, WeaponExtDLL::readLength, &orphan);
		if (orphan)
			Debug::Log("[WeaponExt] %s: %d ROF.At[...].Ease/.Power key(s) "
				"reference a range with no plot point.\n", section, orphan);
	}

	// ---- Section 7: auto-fire range window ----
	this->Range_AutoFire_Min.Read(exINI, section, "Range.AutoFire.Min");
	this->Range_AutoFire_Max.Read(exINI, section, "Range.AutoFire.Max");

	if (this->Range_AutoFire_Max >= 0.0 && this->Range_AutoFire_Max < this->Range_AutoFire_Min)
	{
		Debug::Log("[WeaponExt] %s: Range.AutoFire.Max (%.2f) < Min (%.2f); "
			"the unit would never auto-engage. Ignoring Max.\n",
			section, this->Range_AutoFire_Max.Get(), this->Range_AutoFire_Min.Get());
		this->Range_AutoFire_Max = -1.0;
	}

	// ---- Section 18: InaccuracyModifier ----
	this->InaccuracyModifier.Read(exINI, section, "InaccuracyModifier");
	if (this->InaccuracyModifier < 0.0)
	{
		Debug::Log("[WeaponExt] %s: InaccuracyModifier=%.2f is negative; "
			"clamped to 0 (perfect accuracy).\n", section, this->InaccuracyModifier.Get());
		this->InaccuracyModifier = 0.0;
	}

	// ---- Section 8: friendly-fire risk gate ----
	this->FriendlyFire_Check.Read(exINI, section, "FriendlyFire.Check");
	this->FriendlyFire_MaxRatio.Read(exINI, section, "FriendlyFire.MaxRatio");
	this->FriendlyFire_IncludeScatter.Read(exINI, section, "FriendlyFire.IncludeScatter");
	this->FriendlyFire_Protected.Read(exINI, section, "FriendlyFire.Protected");
	this->FriendlyFire_Protected_Weight.Read(exINI, section, "FriendlyFire.Protected.Weight");
	this->FriendlyFire_MinTargetValue.Read(exINI, section, "FriendlyFire.MinTargetValue");

	if (pINI->ReadString(section, "FriendlyFire.Scope", "",
		WeaponExtDLL::readBuffer, WeaponExtDLL::readLength))
	{
		int scope = 0;
		for (char* tok = strtok(WeaponExtDLL::readBuffer, ",");
			tok != nullptr; tok = strtok(nullptr, ","))
		{
			while (*tok == ' ') ++tok;
			if (Matches(tok, "Owner"))        scope |= ScatterHouse_Owner;
			else if (Matches(tok, "Ally"))    scope |= ScatterHouse_Ally;
			else if (Matches(tok, "Enemy"))   scope |= ScatterHouse_Enemy;
			else if (Matches(tok, "Neutral")) scope |= ScatterHouse_Neutral;
			else if (*tok)
			{
				Debug::Log("[WeaponExt] %s: unrecognised FriendlyFire.Scope entry '%s' "
					"(expected Owner|Ally|Enemy|Neutral).\n", section, tok);
			}
		}
		if (scope)
			this->FriendlyFire_Scope = scope;
	}
	if (this->FriendlyFire_Scope == 0)
		this->FriendlyFire_Scope = ScatterHouse_Owner | ScatterHouse_Ally;

	// Echo what was actually parsed. The engine drops the final line of an INI,
	// so a tag that "should" be set sometimes simply is not -- seeing the
	// parsed value is the fastest way to catch that.
	if (this->HasRangeScatter() || this->HasROFCurve()
		|| this->Range_AutoFire_Max >= 0.0 || this->FriendlyFire_Check)
	{
		Debug::Log("[WeaponExt] %s: rangeScatter=%d(region %.1f-%.1f inc %.2f sys %d mode %d dh %.0f%%) "
			"rof=%d autoFire=[%.1f,%.1f] ff=%d\n",
			section,
			this->HasRangeScatter() ? 1 : 0,
			this->RangeScatter_RegionMin.Get(), this->RangeScatter_RegionMax.Get(),
			this->RangeScatter_Increment.Get(), this->RangeScatter_System.Get(),
			this->RangeScatter_IncrementMode.Get(), this->RangeScatter_DirectHitPercentage.Get(),
			this->HasROFCurve() ? 1 : 0,
			this->Range_AutoFire_Min.Get(), this->Range_AutoFire_Max.Get(),
			this->FriendlyFire_Check ? 1 : 0);
	}

	if (this->RangeScatter_Curve.IsSet() || this->ROF_Curve.IsSet())
	{
		Debug::Log("[WeaponExt] %s: plot curves -- RangeScatter %d point(s), ROF %d point(s)\n",
			section, this->RangeScatter_Curve.Count, this->ROF_Curve.Count);
	}
}

template <typename T>
void WeaponTypeExt::ExtData::Serialize(T& Stm)
{
	Stm
		.Process(this->RangeScatter_RegionMin)
		.Process(this->RangeScatter_RegionMax)
		.Process(this->RangeScatter_Increment)
		.Process(this->RangeScatter_System)
		.Process(this->RangeScatter_IncrementMode)
		.Process(this->RangeScatter_DirectHitPercentage)
		.Process(this->RangeScatter_Curve)
		.Process(this->ROF_RegionMin)
		.Process(this->ROF_RegionMax)
		.Process(this->ROF_Increment)
		.Process(this->ROF_System)
		.Process(this->ROF_IncrementMode)
		.Process(this->ROF_Curve)
		.Process(this->Range_AutoFire_Min)
		.Process(this->Range_AutoFire_Max)
		.Process(this->InaccuracyModifier)
		.Process(this->FriendlyFire_Check)
		.Process(this->FriendlyFire_MaxRatio)
		.Process(this->FriendlyFire_Scope)
		.Process(this->FriendlyFire_IncludeScatter)
		.Process(this->FriendlyFire_Protected)
		.Process(this->FriendlyFire_Protected_Weight)
		.Process(this->FriendlyFire_MinTargetValue)
		;
}

void WeaponTypeExt::ExtData::LoadFromStream(PhobosStreamReader& Stm)
{
	Extension<WeaponTypeClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void WeaponTypeExt::ExtData::SaveToStream(PhobosStreamWriter& Stm)
{
	Extension<WeaponTypeClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

// ============================================================================
// Container lifecycle. All four frameworks (Antares/Ares/Kratos/Phobos) hook
// these same addresses for their own containers and all return 0; Syringe
// chains them, so co-tenancy here is established practice rather than a risk.
// None of these stolen-byte windows contains a relative branch.
// ============================================================================

DEFINE_HOOK(0x771EE9, WeaponTypeClass_CTOR_ScatterExt, 0x5)
{
	GET(WeaponTypeClass*, pItem, ESI);

	WeaponTypeExt::ExtMap.TryAllocate(pItem);

	return 0;
}

DEFINE_HOOK(0x77311D, WeaponTypeClass_SDDTOR_ScatterExt, 0x6)
{
	GET(WeaponTypeClass*, pItem, ESI);

	WeaponTypeExt::ExtMap.Remove(pItem);

	return 0;
}

DEFINE_HOOK_AGAIN(0x772EB0, WeaponTypeClass_SaveLoad_Prefix_ScatterExt, 0x5)
DEFINE_HOOK(0x772CD0, WeaponTypeClass_SaveLoad_Prefix_ScatterExt, 0x7)
{
	GET_STACK(WeaponTypeClass*, pItem, 0x4);
	GET_STACK(IStream*, pStm, 0x8);

	WeaponTypeExt::ExtMap.PrepareStream(pItem, pStm);

	return 0;
}

DEFINE_HOOK(0x772EA6, WeaponTypeClass_Load_Suffix_ScatterExt, 0x6)
{
	WeaponTypeExt::ExtMap.LoadStatic();

	return 0;
}

DEFINE_HOOK(0x772F8C, WeaponTypeClass_Save_ScatterExt, 0x5)
{
	WeaponTypeExt::ExtMap.SaveStatic();

	return 0;
}

DEFINE_HOOK_AGAIN(0x7729C7, WeaponTypeClass_LoadFromINI_ScatterExt, 0x5)
DEFINE_HOOK(0x7729B0, WeaponTypeClass_LoadFromINI_ScatterExt, 0x5)
{
	GET(WeaponTypeClass*, pItem, ESI);
	GET_STACK(CCINIClass*, pINI, 0xE4);

	ScatterDiag::CountWeaponParse();
	WeaponTypeExt::ExtMap.LoadFromINI(pItem, pINI);

	return 0;
}
