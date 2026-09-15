// Section 6: rate of fire as a curve over range.
//
// DESIGN.md originally flagged this as blocked on a feasibility question --
// whether TechnoClass::GetROF has a target in scope, since a rate of fire is
// normally computed without one. The registry answered it: the ROF multiplier
// is applied INSIDE TechnoClass::Fire, not inside GetROF. Antares and Ares
// hook 0x6FF28F as TechnoClass_Fire_BerserkROFMultiplier, and the sequence is:
//
//   6FF289  call [edx+0x318]        ; virtual RearmDelay(weaponIndex) -> EAX
//   6FF28F  mov  cl,[esi+0x298]     ; berserk flag        (Antares/Ares hook)
//   6FF299  sar  eax,1              ; berserk halves the delay
//   6FF29E  mov  [esi+0x2f8],eax    ; STORE the rearm timer
//
// So the delay is computed, adjusted, and only then written to [esi+0x2F8].
// Hooking the store lets us scale EAX first, inside a frame where the target
// and weapon index are already available -- the same TechnoClass::Fire frame
// mapped and confirmed in DESIGN.md sections 2 and 14. GetROF never needed to
// know the target, because Fire does.

#include <TechnoClass.h>
// Required even though nothing here names FootClass -- TechnoClass.h pulls
// Helpers/Cast.h, which instantiates generic_cast<const FootClass*> and needs
// the complete type. See Hooks.FrameProbe.cpp.
#include <FootClass.h>
#include <WeaponTypeClass.h>
#include <AbstractClass.h>

#include <Utilities/Macro.h>

#include <Curve.h>
#include <Ext/WeaponType/Body.h>
#include <ScatterExtDiag.h>

#include <cmath>

namespace
{
	constexpr double LeptonsPerCell = 256.0;
}

// 0x6FF29E -- `mov [esi+0x2f8],eax`, exactly 6 bytes, no relative branch, so
// `return 0` is safe: Syringe re-executes the store with whatever EAX we leave
// behind.
//
// Co-tenants: Phobos (TechnoClass_FireAt_ChargeTurret2) and Kratos
// (TechnoClass_Fire_ROFMultiplier), both size 6. Kratos is not in the current
// injection list. Deliberately NOT 0x6FF28F, which Antares and Ares both own.
DEFINE_HOOK(0x6FF29E, TechnoClass_Fire_ScatterExtROF, 0x6)
{
	GET(TechnoClass*, pFirer, ESI);

	if (!pFirer)
		return 0;

	// EBP-relative: TechnoClass::Fire does `and esp,0xFFFFFFF8`, so ebp-esp is
	// a runtime value and the arguments are unreachable through esp.
	const int weaponIndex = R->Base<int>(0xC);
	auto pTarget = R->Base<AbstractClass*>(0x8);

	auto pWeaponStruct = pFirer->GetWeapon(weaponIndex);
	if (!pWeaponStruct || !pWeaponStruct->WeaponType)
		return 0;

	auto pWeaponExt = WeaponTypeExt::ExtMap.Find(pWeaponStruct->WeaponType);
	if (!pWeaponExt || !pWeaponExt->HasROFCurve())
		return 0;   // untouched: not configured, so vanilla ROF stands

	if (!pTarget)
		return 0;

	// Distance from absolute positions rather than from stack slots. The aim
	// vector at [esp+0x30..0x38] belongs to the scatter block earlier in this
	// function and its esp offsets are not valid here. GetCoords is a virtual
	// on AbstractClass, so it works for a cell target (force-fire on ground)
	// as well as an object, and being unqualified it dispatches to the game.
	const CoordStruct firerPos = pFirer->GetCoords();
	const CoordStruct targetPos = pTarget->GetCoords();

	const double dx = (double)firerPos.X - targetPos.X;
	const double dy = (double)firerPos.Y - targetPos.Y;
	const double dz = (double)firerPos.Z - targetPos.Z;
	const double distCells = std::sqrt(dx * dx + dy * dy + dz * dz) / LeptonsPerCell;

	// The multiplier is 1.0 below Region.min, so a weapon inside its
	// comfortable band fires at exactly its configured ROF -- no rounding
	// drift, mirroring how the accurate zone works for scatter.
	const auto system = (ScatterSystem)pWeaponExt->ROF_System.Get();
	const auto mode = (IncrementMode)pWeaponExt->ROF_IncrementMode.Get();
	const double inc = pWeaponExt->ROF_Increment;

	double multiplier;
	if (pWeaponExt->ROF_Curve.IsSet())
	{
		// Section 21: the drawn curve's value IS the multiplier at this range,
		// so `ROF.At[30]=2.0` means "twice the delay at 30 cells". No Increment
		// interpolation -- the point already says what it means.
		multiplier = pWeaponExt->ROF_Curve.Evaluate(distCells);
	}
	else if (mode == IncrementMode::Peak)
	{
		// Increment is the multiplier reached at the curve's peak: 2.0 means
		// twice the delay (half the fire rate) there. Interpolating from 1
		// rather than scaling by shape() directly is what makes Increment < 1
		// speed the weapon up instead of driving the delay to zero.
		const double t = Curve::Progress(distCells,
			pWeaponExt->ROF_RegionMin, pWeaponExt->ROF_RegionMax);
		const double shape = distCells < pWeaponExt->ROF_RegionMin
			? 0.0 : Curve::Shape(system, t);
		multiplier = 1.0 + (inc - 1.0) * shape;
	}
	else
	{
		// Rate: unbounded growth per cell beyond Region.min.
		multiplier = 1.0 + Curve::Evaluate(system, IncrementMode::Rate, inc,
			distCells, pWeaponExt->ROF_RegionMin, pWeaponExt->ROF_RegionMax);
	}

	if (multiplier < 0.0)
		multiplier = 0.0;

	GET(int, delay, EAX);
	const int scaled = (int)(delay * multiplier);

	// Never return a negative delay, and never return 0 for a weapon that had
	// a real one: a zero rearm timer is a weapon that fires every frame, which
	// is a far worse failure than a slightly wrong rate.
	const int finalDelay = scaled < 1 ? (delay > 0 ? 1 : delay) : scaled;

	R->EAX(finalDelay);

	ScatterDiag::ROFLine(pWeaponStruct->WeaponType->ID, distCells,
		delay, finalDelay, multiplier);

	return 0;
}
