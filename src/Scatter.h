#pragma once

class TechnoClass;

// Phase 1: the replacement aim vector.
//
// Applied == false means "we are not configured for this shot" -- the caller
// must leave the engine's own result untouched. That path consumes NO random
// numbers, so a weapon with none of our tags produces a bit-identical game to
// one running without this DLL.
struct ScatterResult
{
	bool Applied;
	// True when the DirectHitPercentage roll won, i.e. we deliberately placed
	// the shot exactly on target. Distinct from "no scatter because the target
	// is inside the accurate zone", and both are distinct from "the engine did
	// nothing" -- all three produce delta (0,0,0) and were previously
	// indistinguishable in the log.
	bool DirectHit;
	int X, Y, Z;   // leptons, firer-relative delta (same frame as the engine's)
};

// Recompute the scattered aim vector from the UNSCATTERED one.
//
// `aim*` is the engine's untouched firer->target delta at [esp+0x30..0x38].
// `eng*` is what the engine assembled at [esp+0x94..0x9C]; it is used only when
// per-axis shaping is configured WITHOUT RangeScatter, in which case the
// engine's own magnitude (including Antares' BallisticScatter.Min/.Max, and
// whichever of the two vanilla paths ran) is reshaped rather than replaced.
ScatterResult ComputeScatter(TechnoClass* pFirer, int weaponIndex,
	int aimX, int aimY, int aimZ,
	int engX, int engY, int engZ);
