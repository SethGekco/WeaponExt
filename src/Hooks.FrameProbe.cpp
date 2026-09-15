// The ScatterExt hook at 0x6FE8EE: Phase 1 behaviour plus the Phase 0 probe.
//
// NO LONGER READ-ONLY. When a weapon carries our tags this overwrites the
// assembled aim vector at [esp+0x94..0x9C]. When it does not, it touches
// nothing and draws no random numbers, so unconfigured weapons remain
// bit-identical to a game without this DLL -- including their consumption of
// the synced RNG stream, which is what keeps multiplayer in step.
//
// The frame map it relies on (ESI = firer, [ebp+0x8] = target,
// [ebp+0xC] = weapon index, [esp+0x30..0x38] = the unscattered delta, both
// scatter paths converging with identical esp) was confirmed in-game across
// four runs -- see DESIGN.md sections 14 and 15. The probe stays because it is
// now the regression check on all of it, and the before/after logging is how
// Phase 1 itself gets verified.

#include <TechnoClass.h>
// Required even though nothing here names FootClass. TechnoClass.h pulls
// Helpers/Template.h -> Helpers/Cast.h, whose APPLY_GC_ABSTRACT_CAST(FootClass*)
// at Cast.h:126 instantiates generic_cast<const FootClass*>, and that needs
// FootClass COMPLETE (is_base_of, is_abstract, Base::AbsDerivateID).
// TechnoClass.h only forward-declares it. MSVC instantiates those bodies at
// end of translation unit, so FootClass merely has to become complete
// somewhere in the TU -- omitting this include is 6 errors inside YRpp.
#include <FootClass.h>
#include <TechnoTypeClass.h>
#include <WeaponTypeClass.h>  // WeaponStruct::WeaponType->ID
#include <AbstractClass.h>
#include <ObjectClass.h>

#include <Utilities/Macro.h>

#include <ScatterExtDiag.h>
#include <Scatter.h>

#include <cmath>

namespace
{
	// Best-effort name for logging. Anything that is not an ObjectClass with a
	// type just logs as unknown rather than risking a bad dereference -- this
	// is a diagnostic, it must never be the thing that crashes the game.
	const char* SafeName(AbstractClass* pAbs)
	{
		if (!pAbs)
			return nullptr;

		const auto rtti = pAbs->WhatAmI();
		switch (rtti)
		{
		case AbstractType::Unit:
		case AbstractType::Infantry:
		case AbstractType::Building:
		case AbstractType::Aircraft:
			break;
		default:
			return "<non-techno>";
		}

		auto pTechno = static_cast<TechnoClass*>(pAbs);
		auto pType = pTechno->GetTechnoType();
		return pType ? pType->ID : "<no-type>";
	}
}

// 0x6FE8EE -- the instruction after the scattered aim vector is fully
// assembled, and before atan2 at 0x6FE902 consumes it.
//
// Stolen bytes are `8B 91 DC 02 00 00` (mov edx,[ecx+0x2dc]) -- 6 bytes with
// NO relative branch, so `return 0` is safe here. That is not true of the
// EvaluateObject gates this project will need later (see the trampoline
// footgun in the Hook Encyclopedia's Target-Evaluation-Threat.md): a stolen
// jcc re-executed from Syringe's stub computes its target relative to the
// stub and jumps into garbage.
DEFINE_HOOK(0x6FE8EE, TechnoClass_Fire_ScatterExtFrameProbe, 0x6)
{
	// First site we have PROVEN executes and logs, so the init-hook report is
	// emitted from here rather than from any of the init hooks themselves.
	ScatterDiag::ReportOnce();

	GET(TechnoClass*, pFirer, ESI);

	// EBP-relative, NOT esp-relative. TechnoClass::Fire does
	// `and esp,0xFFFFFFF8` at 0x6FDD53, so ebp-esp is a runtime value and the
	// args cannot be reached through GET_STACK. R->Base<T>() reads off _EBP;
	// R->Stack<T>() reads off _ESP. Mixing them up here would read garbage.
	auto pTarget = R->Base<AbstractClass*>(0x8);
	const int weaponIndex = R->Base<int>(0xC);

	// esp-relative reads. Both scatter paths converge here with identical esp
	// (DESIGN.md section 2), so these offsets are path-agnostic -- that is
	// itself one of the claims under test.
	const int aimX = R->Stack<int>(0x30);
	const int aimY = R->Stack<int>(0x34);
	const int aimZ = R->Stack<int>(0x38);

	const int outX = R->Stack<int>(0x94);
	const int outY = R->Stack<int>(0x98);
	const int outZ = R->Stack<int>(0x9C);

	// Resolve the weapon's INI name. GetWeapon is a virtual called unqualified
	// through the pointer, so it dispatches to the game's implementation --
	// the R0 stub in ObjectClass.h only bites on qualified calls. Guarded at
	// every step: a diagnostic must never be the thing that crashes the game.
	//
	// Resolved BEFORE the budget decision because the budget is now per-weapon.
	const char* weaponName = nullptr;
	if (pFirer)
	{
		if (auto pWeaponStruct = pFirer->GetWeapon(weaponIndex))
		{
			if (pWeaponStruct->WeaponType)
				weaponName = pWeaponStruct->WeaponType->ID;
		}
	}

	// ---- Phase 1: replace the aim vector -------------------------------
	// Recomputed from the UNSCATTERED delta, so whatever the engine (and
	// Antares) produced is simply discarded rather than adjusted. Returns
	// Applied == false, having drawn no random numbers, when this weapon has
	// none of our tags -- so unconfigured weapons stay bit-identical to
	// vanilla, including their consumption of the synced RNG stream.
	const auto result = ComputeScatter(pFirer, weaponIndex,
		aimX, aimY, aimZ, outX, outY, outZ);

	if (result.Applied)
	{
		R->Stack(0x94, result.X);
		R->Stack(0x98, result.Y);
		R->Stack(0x9C, result.Z);
	}

	const int finX = result.Applied ? result.X : outX;
	const int finY = result.Applied ? result.Y : outY;
	const int finZ = result.Applied ? result.Z : outZ;

	// "Interesting" means ScatterExt acted OR the shot moved -- not merely that
	// it moved. A direct-hit override and a shot inside the accurate zone both
	// land exactly on target, so under the older "did it move" rule they fell
	// into the small unscattered budget and competed with rifle fire. That
	// budget filled at 40/40 in the 2026-09-06 run, which is why zero direct
	// hits were observed where ~3 were expected: they were not absent, they
	// were unobservable.
	const bool moved = (finX != aimX) || (finY != aimY) || (finZ != aimZ);
	const bool interesting = result.Applied || moved;

	if (!ScatterDiag::ShouldLog(interesting, weaponName))
		return 0;

	const double dist = std::sqrt(
		(double)aimX * aimX + (double)aimY * aimY + (double)aimZ * aimZ);

	ScatterDiag::ProbeLine(
		SafeName(pFirer), SafeName(pTarget), weaponIndex, weaponName,
		aimX, aimY, aimZ,
		outX, outY, outZ,
		finX, finY, finZ, result.Applied, result.DirectHit,
		dist);

	return 0;
}
