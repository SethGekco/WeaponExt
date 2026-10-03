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
		// Frames since the beam last hit this victim. The magnetron re-imbues
		// on every shot, so "no fresh imbue for a while" IS "the beam
		// stopped" -- see the long comment on the release condition below.
		int FramesSinceImbue;
		bool NeedsFlightOverrides;
		// Handoff bookkeeping: last seen position, how long it has sat still,
		// and a latch so the swap happens exactly once per hold.
		CoordStruct LastCoords;
		int StillFrames;
		bool HandedOff;
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

	// ImbueLocomotor is handed a bare CLSID, not the warhead that chose it,
	// so the per-warhead tags have to be found another way. The firer just
	// fired a locomotor weapon, so we take the first weapon it owns whose
	// warhead has IsLocomotor=yes. In practice a magnetron has exactly one;
	// a unit with two different locomotor warheads gets the first, which is
	// documented rather than guessed at.
	WarheadTypeClass* FindLocomotorWarhead(TechnoClass* pFirer)
	{
		for (int i = 0; i < 18; ++i)
		{
			if (auto const pStruct = pFirer->GetWeapon(i))
			{
				if (auto const pWeapon = pStruct->WeaponType)
				{
					if (auto const pWH = pWeapon->Warhead)
					{
						if (pWH->IsLocomotor)
							return pWH;
					}
				}
			}
		}

		return nullptr;
	}

	void ApplyFlightOverrides(FootClass* pVictim, WarheadTypeExt::ExtData* pExt,
		WarheadTypeClass* pWH)
	{
		if (!pExt->HasFlightOverrides() || !Magnetron::WarheadUsesJumpjet(pWH))
			return;

		auto const pILoco = pVictim->Locomotor.GetInterfacePtr();
		if (!pILoco)
			return;

		// static_cast, not reinterpret_cast: LocomotionClass derives from
		// IPersistStream *then* ILocomotion, so the stored ILocomotion
		// pointer is offset into the object and the compiler has to apply the
		// base adjustment. Doing this by hand is how you corrupt a locomotor.
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

bool Magnetron::WarheadUsesJumpjet(WarheadTypeClass* pWH)
{
	return pWH && pWH->IsLocomotor
		&& IsEqualGUID(pWH->Locomotor, JumpjetLocomotorCLSID);
}

void Magnetron::OnImbued(TechnoClass* pFirer, FootClass* pVictim)
{
	if (!pFirer || !pVictim)
		return;

	auto const pWH = FindLocomotorWarhead(pFirer);
	if (!pWH)
		return;

	auto const pExt = WarheadTypeExt::ExtMap.Find(pWH);
	if (!pExt || !pExt->HasAnyMagnetron())
		return;   // vanilla behaviour, byte for byte

	// Re-arm an existing entry rather than duplicating it: re-grabbing the
	// same victim every shot is exactly how a magnetron holds something, and
	// resetting FramesSinceImbue here is what makes "the beam is still on
	// it" observable at all.
	for (int i = 0; i < HoldCount; ++i)
	{
		if (Holds[i].Victim == pVictim)
		{
			Holds[i].Firer = pFirer;
			Holds[i].Warhead = pWH;
			Holds[i].FramesSinceImbue = 0;
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

	// Flight overrides are deferred to the next PerFrame tick: this runs at
	// ImbueLocomotor's *entry*, so the new locomotor does not exist yet.
	Holds[HoldCount++] = Hold { pVictim, pFirer, pWH, 0, 0, true,
		pVictim->GetCoords(), 0, false };
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

		// Apply the deferred flight overrides now that the locomotor exists.
		if (hold.NeedsFlightOverrides)
		{
			hold.NeedsFlightOverrides = false;
			ApplyFlightOverrides(pVictim, pExt, hold.Warhead);
		}

		++hold.HeldFrames;
		++hold.FramesSinceImbue;

		// --- movement tracking (for the "stopped" handoff trigger) --------
		auto const coords = pVictim->GetCoords();
		if (coords.X == hold.LastCoords.X && coords.Y == hold.LastCoords.Y
			&& coords.Z == hold.LastCoords.Z)
		{
			++hold.StillFrames;
		}
		else
		{
			hold.StillFrames = 0;
			hold.LastCoords = coords;
		}

		// --- handoff ------------------------------------------------------
		// Rex's idea, and it is the thing that makes every non-jumpjet CLSID
		// usable: let the imbued locomotor do the travelling, then swap to
		// the jumpjet purely so its release path runs. With Lift=0 the player
		// sees no vertical movement at all -- it is just a clean handback.
		if (pExt->Magnetron_Handoff && !hold.HandedOff
			&& !WarheadUsesJumpjet(hold.Warhead))
		{
			const bool stopped = pExt->Magnetron_Handoff_OnStopped
				&& hold.StillFrames >= pExt->Magnetron_Handoff_StoppedFor;

			bool arrived = false;
			if (pExt->Magnetron_Handoff_OnArrived)
			{
				auto const firerCoords = pFirer->GetCoords();
				const double dx = static_cast<double>(coords.X - firerCoords.X);
				const double dy = static_cast<double>(coords.Y - firerCoords.Y);
				const double limit = pExt->Magnetron_Handoff_ArriveRange * 256.0;
				arrived = (dx * dx + dy * dy) <= (limit * limit);
			}

			const bool controlled = pExt->Magnetron_Handoff_OnMindControl
				&& pVictim->MindControlledBy != nullptr;

			if (stopped || arrived || controlled)
			{
				hold.HandedOff = true;

				// Re-imbue with the jumpjet. ImbueLocomotor tears down the
				// victim's existing locomotor and link state first (verified
				// at 0x710017-0x710297), so calling it mid-hold is safe; it
				// is also exactly what Phobos does from C++.
				pFirer->ImbueLocomotor(pVictim, JumpjetLocomotorCLSID);

				if (auto const pILoco = pVictim->Locomotor.GetInterfacePtr())
				{
					auto const pJJ = static_cast<JumpjetLocomotionClass*>(pILoco);

					// Lift=0 keeps it invisible; any positive value gives a
					// visible hop before the drop.
					pJJ->Height = pExt->Magnetron_Handoff_Lift;

					if (pExt->Magnetron_Handoff_Crash.isset())
						pJJ->Crash = static_cast<float>(pExt->Magnetron_Handoff_Crash.Get());
				}

				// Deliberately NOT releasing here: the point is to let the
				// jumpjet's own release path run, which is the only one that
				// restores command properly. ReleaseOnStop and MaxHoldTime
				// stay armed as backstops in case it does not reach it.
				WeaponDiag::MagnetronLine(
					stopped ? "handoff:stopped"
						: arrived ? "handoff:arrived" : "handoff:mindcontrol",
					pVictim->GetTechnoType()->ID,
					pFirer->GetTechnoType() ? pFirer->GetTechnoType()->ID : "<none>");

				continue;
			}
		}

		const int maxHold = pExt->Magnetron_MaxHoldTime;
		if (maxHold >= 0 && hold.HeldFrames > maxHold)
		{
			DoRelease(pFirer, pVictim, "max-hold");
			Forget(i);
			continue;
		}

		if (pExt->Magnetron_ReleaseOnStop)
		{
			// ⚠ The signal is "no fresh imbue recently", NOT "the firer
			// stopped targeting it". The first version compared
			// `pFirer->Target == pVictim` and never fired: a magnetron goes
			// on targeting the unit it is holding, so the condition stayed
			// false forever.
			//
			// Every shot re-imbues, so the beam IS the stream of imbues.
			// Delay therefore has to exceed the weapon's ROF or the victim
			// gets dropped between shots -- which is why the default is 45
			// rather than the 15 it shipped with (ROF=20 is common and would
			// have thrashed).
			if (hold.FramesSinceImbue > pExt->Magnetron_ReleaseOnStop_Delay)
			{
				DoRelease(pFirer, pVictim, "beam-stopped");
				Forget(i);
				continue;
			}
		}
	}
}
