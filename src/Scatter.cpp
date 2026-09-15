#include "Scatter.h"

#include <TechnoClass.h>
// Required even though nothing here names FootClass -- see the comment in
// Hooks.FrameProbe.cpp. TechnoClass.h -> Helpers/Template.h -> Helpers/Cast.h
// instantiates generic_cast<const FootClass*>, which needs the complete type.
#include <FootClass.h>
#include <WeaponTypeClass.h>
#include <BulletTypeClass.h>
#include <ScenarioClass.h>

#include <Curve.h>
#include <Ext/WeaponType/Body.h>
#include <Ext/BulletType/Body.h>
#include <Ext/TechnoType/Body.h>

#include <cmath>

namespace
{
	constexpr double Pi = 3.14159265358979323846;
	// NOT `Leptons` -- YRpp already defines a Leptons TYPE (Antares uses
	// Nullable<Leptons> for BallisticScatter.Min/.Max), and the collision is an
	// ambiguous-symbol error at every use site.
	constexpr double LeptonsPerCell = 256.0;

	// Every draw goes through the SYNCED game RNG. Using anything else -- a
	// std:: engine, a render-side source -- desyncs multiplayer, and scatter is
	// a particularly nasty place for it because the divergence is invisible
	// until impact coordinates drift apart.
	int SyncedRandom(int lo, int hi)
	{
		if (hi < lo)
		{
			const int t = lo; lo = hi; hi = t;
		}
		return ScenarioClass::Instance->Random.RandomRanged(lo, hi);
	}

	int RandLeptons(double loCells, double hiCells)
	{
		return SyncedRandom((int)(loCells * LeptonsPerCell), (int)(hiCells * LeptonsPerCell));
	}

	// Uniform angle, quantised to 1/65536 of a circle exactly as the engine
	// does at 0x6FE838. Preserving that quantisation keeps our angles in the
	// same representable set as vanilla's.
	double SyncedAngle()
	{
		return SyncedRandom(0, 65535) * (2.0 * Pi / 65536.0);
	}
}

ScatterResult ComputeScatter(TechnoClass* pFirer, int weaponIndex,
	int aimX, int aimY, int aimZ,
	int engX, int engY, int engZ)
{
	ScatterResult none { false, false, aimX, aimY, aimZ };

	if (!pFirer)
		return none;

	auto pWeaponStruct = pFirer->GetWeapon(weaponIndex);
	if (!pWeaponStruct || !pWeaponStruct->WeaponType)
		return none;

	auto pWeapon = pWeaponStruct->WeaponType;
	auto pWeaponExt = WeaponTypeExt::ExtMap.Find(pWeapon);

	auto pProjectile = pWeapon->Projectile;
	auto pBulletExt = pProjectile ? BulletTypeExt::ExtMap.Find(pProjectile) : nullptr;

	const bool hasRange = pWeaponExt && pWeaponExt->HasRangeScatter();
	const bool hasAxis = pBulletExt && pBulletExt->HasPerAxis();

	// --- 0. InaccuracyModifier (DESIGN.md section 18) ----------------------
	// Product of every applicable source. Multiplicative because these are
	// proportional effects: two independent "half as inaccurate" sources
	// should give a quarter, not zero.
	double modifier = pWeaponExt ? pWeaponExt->InaccuracyModifier.Get() : 1.0;

	if (auto pType = pFirer->GetTechnoType())
	{
		if (auto pTypeExt = TechnoTypeExt::ExtMap.Find(pType))
		{
			modifier *= pTypeExt->ForRank(
				pFirer->Veterancy.IsVeteran(), pFirer->Veterancy.IsElite());
		}
	}

	if (modifier < 0.0)
		modifier = 0.0;

	const bool hasModifier = modifier != 1.0;

	// EARLY OUT BEFORE ANY RNG DRAW. A weapon with none of our tags must leave
	// the synced random stream exactly as vanilla left it.
	if (!hasRange && !hasAxis && !hasModifier)
		return none;

	// Modifier-only: scale whatever the engine (or Antares) already produced.
	// This is what makes InaccuracyModifier apply to plain vanilla
	// BallisticScatter, and it needs NO random draw -- we are scaling a vector
	// that has already been generated, so the synced stream is untouched and a
	// weapon carrying only this tag stays in step with a client that has none.
	if (!hasRange && !hasAxis)
	{
		return ScatterResult {
			true,
			modifier == 0.0,   // a zero modifier is a deliberate direct hit
			aimX + (int)((engX - aimX) * modifier),
			aimY + (int)((engY - aimY) * modifier),
			aimZ + (int)((engZ - aimZ) * modifier)
		};
	}

	// Perfect accuracy short-circuits the whole computation, and must do so
	// BEFORE any draw so that the stream cost does not depend on the modifier.
	if (modifier == 0.0)
		return ScatterResult { true, true, aimX, aimY, aimZ };

	const double distCells =
		std::sqrt((double)aimX * aimX + (double)aimY * aimY + (double)aimZ * aimZ) / LeptonsPerCell;

	// --- 1. Direct-hit override -------------------------------------------
	// A flat chance to ignore the curve entirely and hit the aim point.
	//
	// Only rolled where the curve would actually scatter. Inside the accurate
	// zone the shot lands exactly on target regardless, so a "direct hit"
	// there is a meaningless label on an outcome that was already guaranteed --
	// the 2026-09-06 run logged 7 direct hits of which only 4 were beyond the
	// zone, which made the observed rate impossible to compare against the
	// configured one without filtering by distance first.
	//
	// This also matches the order DESIGN.md section 4 specifies:
	// accurate zone, then direct-hit roll, then curve.
	if (hasRange && distCells >= pWeaponExt->RangeScatter_RegionMin)
	{
		const double chance = pWeaponExt->RangeScatter_DirectHitPercentage;
		if (chance > 0.0 && SyncedRandom(1, 100) <= (int)(chance + 0.5))
			return ScatterResult { true, true, aimX, aimY, aimZ };
	}

	// --- 2. Magnitude ------------------------------------------------------
	// Everything below works in UNSCALED cells plus a single `scale` factor
	// applied once at the end. Keeping the two separate matters: an earlier
	// version folded the modifier into both the base magnitude and the axis
	// scale and applied it twice, and folded the curve into both as well,
	// which made an unset axis scale quadratically with Increment.
	//
	//   unscaledDefault = the magnitude an axis gets when it sets no Min/Max
	//   scale           = curve shape x InaccuracyModifier
	double unscaledDefault;
	double scale;

	if (hasRange && pWeaponExt->RangeScatter_Curve.IsSet())
	{
		// Section 21: a drawn curve states the magnitude at this range outright.
		// There is no Increment to normalise against, so the curve IS the
		// default and the only remaining factor is the modifier. Normalising
		// here would double-apply the range dependence.
		unscaledDefault = pWeaponExt->ExpectedScatterCells(distCells);
		scale = modifier;
	}
	else if (hasRange)
	{
		// shape(t) recovered by dividing the curve output by Increment, so the
		// ellipse collapses at a Parabola's trough and is full size at its peak.
		const double inc = pWeaponExt->RangeScatter_Increment;
		const double curveCells = pWeaponExt->ExpectedScatterCells(distCells);

		unscaledDefault = inc;
		scale = (inc != 0.0 ? curveCells / inc : 0.0) * modifier;
	}
	else
	{
		// Per-axis without a curve: reshape whatever the engine already
		// produced, which preserves vanilla / Antares BallisticScatter.Min-.Max.
		unscaledDefault = std::sqrt(
			(double)(engX - aimX) * (engX - aimX) +
			(double)(engY - aimY) * (engY - aimY)) / LeptonsPerCell;
		scale = modifier;
	}

	// --- 3. Shape ----------------------------------------------------------
	const double theta = SyncedAngle();

	// Fixed draw order X, then Y, then Z -- always all three, even when an axis
	// is unset and the value is discarded, so the synced stream advances
	// identically on every client.
	// MaxNAt() returns the drawn curve's value at this range when that axis has
	// one, else the scalar Max, else unscaledDefault -- so X can be a drawn
	// curve while Y stays a flat number.
	const double sX = hasAxis
		? RandLeptons(pBulletExt->ScatterMinX.Get(unscaledDefault),
			pBulletExt->MaxXAt(distCells, unscaledDefault)) * scale
		: unscaledDefault * LeptonsPerCell * scale;
	const double sY = hasAxis
		? RandLeptons(pBulletExt->ScatterMinY.Get(unscaledDefault),
			pBulletExt->MaxYAt(distCells, unscaledDefault)) * scale
		: unscaledDefault * LeptonsPerCell * scale;

	// Z is the odd one out: X and Y get their sign from sin/cos of the angle,
	// but Z has no angle, so magnitude and sign are drawn separately.
	//
	// The obvious shortcut -- RandomRanged(-Max.Z, +Max.Z) -- silently ignores
	// Min.Z, which the design documents and the parser reads. A tag that is
	// accepted and then does nothing is worse than one that is rejected, so
	// draw the magnitude from [Min.Z, Max.Z] like every other axis and give it
	// a sign of its own.
	double sZ = 0.0;
	if (hasAxis)
	{
		const double zMag = RandLeptons(
			pBulletExt->ScatterMinZ.Get(0.0), pBulletExt->MaxZAt(distCells, 0.0));
		sZ = (SyncedRandom(0, 1) != 0 ? zMag : -zMag) * scale;
	}

	// Cross-range (deflection) rides sin, along-range (over/undershoot) rides
	// cos -- matching vanilla's X = aim + s*sin, Y = aim - s*cos so that an
	// unshaped result is indistinguishable from the engine's.
	const double cross = sX * std::sin(theta);
	const double along = sY * std::cos(theta);

	double offX, offY;

	if (hasAxis && pBulletExt->AxisFirerRelative)
	{
		// Rotate the ellipse onto the line of fire, so Y means overshoot and X
		// means deflection regardless of which way the unit is pointing. The
		// aim vector IS the firing direction, so no bearing lookup is needed.
		const double len = std::sqrt((double)aimX * aimX + (double)aimY * aimY);
		if (len < 1.0)
		{
			// Degenerate: target is directly overhead/underfoot, no meaningful
			// bearing. Fall back to world axes rather than divide by ~zero.
			offX = cross;
			offY = -along;
		}
		else
		{
			const double ux = aimX / len, uy = aimY / len;
			offX = along * ux - cross * uy;
			offY = along * uy + cross * ux;
		}
	}
	else
	{
		offX = cross;
		offY = -along;
	}

	return ScatterResult {
		true,
		false,
		aimX + (int)offX,
		aimY + (int)offY,
		aimZ + (int)sZ
	};
}
