#include "Magnetron.h"
#include "WeaponExtDiag.h"

#include <TechnoClass.h>
// TechnoClass.h -> Helpers/Cast.h instantiates generic_cast<const FootClass*>,
// which needs FootClass complete. Required even where we name it anyway.
#include <FootClass.h>
#include <WarheadTypeClass.h>
#include <JumpjetLocomotionClass.h>

#include <Ext/WarheadType/Body.h>

#include <Utilities/Debug.h>

#include <cstddef>

// {92612C46-F71F-11d1-AC9F-006008055BB5}
const CLSID JumpjetLocomotorCLSID =
	{ 0x92612C46, 0xF71F, 0x11D1, { 0xAC, 0x9F, 0x00, 0x60, 0x08, 0x05, 0x5B, 0xB5 } };

// M0 read these two offsets straight out of the disassembly: the jumpjet ctor
// zeroes State at 0x54AC6D and the magnetron-release path writes State=4 and
// CurrentHeight=0 at 0x54C1B3/0x54C1D0. Asserting them here turns that
// runtime fact into a build-time guarantee: if YRpp's layout ever drifts, the
// build breaks loudly instead of us scribbling over the wrong members of a
// live locomotor.
static_assert(offsetof(JumpjetLocomotionClass, State) == 0x50,
	"JumpjetLocomotionClass::State must sit at +0x50 (verified in gamemd.exe)");
static_assert(offsetof(JumpjetLocomotionClass, CurrentHeight) == 0x80,
	"JumpjetLocomotionClass::CurrentHeight must sit at +0x80 (verified in gamemd.exe)");

namespace
{
	// Active holds. A fixed table rather than a container keyed on the victim:
	// holds are inherently few (one per magnetron that is currently firing),
	// and a flat array needs no allocation in a per-frame sweep and cannot
	// dangle as long as every read re-validates liveness.
	constexpr int MaxHolds = 32;

	struct Hold
	{
		FootClass* Victim;
		TechnoClass* Firer;
		WarheadTypeClass* Warhead;
		int HeldFrames;
		int NotTargetedFrames;
	};

	Hold Holds[MaxHolds] = {};
	int HoldCount = 0;
	bool OverflowReported = false;

	void Forget(int i)
	{
		Holds[i] = Holds[HoldCount - 1];
		Holds[HoldCount - 1] = Hold {};
		--HoldCount;
	}

	// The release the engine only knows how to do from inside the jumpjet
	// locomotor. Called on the HOLDER, which is what ReleaseLocomotor expects
	// (ECX = firer); it clears the victim's LocomotorSource, sets the
	// BeingManipulatedBy / ChronoWarpedByHouse fall-kill credit, and clears
	// the firer's LocomotorTarget.
	void DoRelease(TechnoClass* pFirer, FootClass* pVictim, const char* why)
	{
		if (!pFirer)
			return;

		pFirer->ReleaseLocomotor(true);

		WeaponDiag::MagnetronLine(why,
			pVictim ? pVictim->GetTechnoType()->ID : "<none>",
			pFirer->GetTechnoType() ? pFirer->GetTechnoType()->ID : "<none>");
	}
}

bool Magnetron::WarheadUsesJumpjet(WarheadTypeClass* pWH)
{
	return pWH && pWH->IsLocomotor
		&& IsEqualGUID(pWH->Locomotor, JumpjetLocomotorCLSID);
}

void Magnetron::OnImbued(TechnoClass* pFirer, FootClass* pVictim, WarheadTypeClass* pWH)
{
	if (!pFirer || !pVictim || !pWH)
		return;

	auto const pExt = WarheadTypeExt::ExtMap.Find(pWH);
	if (!pExt || !pExt->HasAnyMagnetron())
		return;   // vanilla behaviour, byte for byte

	// --- flight overrides -------------------------------------------------
	// Only legal when the imbued locomotor really is a jumpjet; see the
	// WarheadUsesJumpjet comment.
	if (pExt->HasFlightOverrides() && WarheadUsesJumpjet(pWH))
	{
		if (auto const pILoco = pVictim->Locomotor.GetInterfacePtr())
		{
			// static_cast, not reinterpret_cast: LocomotionClass derives from
			// IPersistStream *then* ILocomotion, so the stored ILocomotion
			// pointer is offset into the object and the compiler has to apply
			// the base adjustment. Doing this by hand is how you corrupt a
			// locomotor.
			auto const pJJ = static_cast<JumpjetLocomotionClass*>(pILoco);

			if (pExt->Magnetron_Speed.isset())
				pJJ->Speed = pExt->Magnetron_Speed;
			if (pExt->Magnetron_Climb.isset())
				pJJ->Climb = static_cast<float>(pExt->Magnetron_Climb.Get());
			if (pExt->Magnetron_Crash.isset())
				pJJ->Crash = static_cast<float>(pExt->Magnetron_Crash.Get());
			if (pExt->Magnetron_Height.isset())
				pJJ->Height = pExt->Magnetron_Height;
			if (pExt->Magnetron_Accel.isset())
				pJJ->Accel = static_cast<float>(pExt->Magnetron_Accel.Get());
			if (pExt->Magnetron_Wobbles.isset())
				pJJ->Wobbles = static_cast<float>(pExt->Magnetron_Wobbles.Get());
			if (pExt->Magnetron_Deviation.isset())
				pJJ->Deviation = pExt->Magnetron_Deviation;
			if (pExt->Magnetron_TurnRate.isset())
				pJJ->TurnRate = pExt->Magnetron_TurnRate;
		}
	}

	// --- register the hold ------------------------------------------------
	if (!pExt->Magnetron_ReleaseOnStop && pExt->Magnetron_MaxHoldTime < 0)
		return;   // nothing to supervise

	// Re-arm an existing entry rather than duplicating it: the same magnetron
	// re-grabbing the same victim is normal (one imbue per shot).
	for (int i = 0; i < HoldCount; ++i)
	{
		if (Holds[i].Victim == pVictim)
		{
			Holds[i].Firer = pFirer;
			Holds[i].Warhead = pWH;
			Holds[i].NotTargetedFrames = 0;
			return;
		}
	}

	if (HoldCount >= MaxHolds)
	{
		if (!OverflowReported)
		{
			OverflowReported = true;
			Debug::Log("[WeaponExt] magnetron: more than %d simultaneous holds; "
				"extra victims fall back to vanilla (never auto-released).\n",
				MaxHolds);
		}
		return;
	}

	Holds[HoldCount++] = Hold { pVictim, pFirer, pWH, 0, 0 };
}

void Magnetron::PerFrame()
{
	for (int i = HoldCount - 1; i >= 0; --i)
	{
		auto& hold = Holds[i];

		auto const pVictim = hold.Victim;
		auto const pFirer = hold.Firer;

		// Liveness first, every frame: ReleaseLocomotor and the engine's own
		// pointer-expiry paths can both have fired since we last looked.
		if (!pVictim || !pFirer || !pVictim->IsAlive || !pFirer->IsAlive)
		{
			Forget(i);
			continue;
		}

		// Someone else already let this victim go (the jumpjet finishing
		// normally, a new grab, or a death) -- nothing left to supervise.
		if (!pVictim->IsAttackedByLocomotor || pVictim->LocomotorSource != pFirer)
		{
			Forget(i);
			continue;
		}

		auto const pExt = WarheadTypeExt::ExtMap.Find(hold.Warhead);
		if (!pExt)
		{
			Forget(i);
			continue;
		}

		++hold.HeldFrames;

		const int maxHold = pExt->Magnetron_MaxHoldTime;
		if (maxHold >= 0 && hold.HeldFrames > maxHold)
		{
			DoRelease(pFirer, pVictim, "max-hold");
			Forget(i);
			continue;
		}

		if (pExt->Magnetron_ReleaseOnStop)
		{
			// "Still firing at it" == the firer is still targeting it. A
			// grace window avoids dropping the victim on a one-frame
			// retarget blip.
			if (pFirer->Target == pVictim)
				hold.NotTargetedFrames = 0;
			else
				++hold.NotTargetedFrames;

			if (hold.NotTargetedFrames > pExt->Magnetron_ReleaseOnStop_Delay)
			{
				DoRelease(pFirer, pVictim, "beam-stopped");
				Forget(i);
				continue;
			}
		}
	}
}
