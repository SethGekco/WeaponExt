#pragma once

// Magnetron (DESIGN.md section 2, phase M1).
//
// M0 established the architecture, and it is much less work than planned:
//
//   * The Magnetron has NO locomotor of its own. `[LocomotorBeam]` imbues the
//     JUMPJET locomotor onto the victim, and JumpjetLocomotionClass holds its
//     own *instance* copies of Speed / Climb / Crash / Height / Accel /
//     Wobbles / Deviation / TurnRate, copied from the victim's TechnoType.
//     So per-weapon flight tuning = overwrite those instance fields after the
//     vanilla imbue. No flight code of our own, and the jumpjet keeps doing
//     the lift, carry, land AND release.
//
//   * There is NO beam-stop detection anywhere in the engine -- vanilla never
//     needs it, because the jumpjet decides when it is finished. So
//     `Magnetron.ReleaseOnStop` is entirely ours, and it is also the safety
//     net for a non-jumpjet `Locomotor=`: those locomotors contain no release
//     code at all, which is why they paralyse the victim permanently.
//
// Full RE writeup: YR-Hook-Encyclopedia/encyclopedia/Magnetron-Locomotor-Imbue.md

#include <guiddef.h>

class TechnoClass;
class FootClass;
class WarheadTypeClass;

// {92612C46-F71F-11d1-AC9F-006008055BB5} -- JumpjetLocomotionClass, which is
// what the stock magnetron warhead actually specifies.
extern const CLSID JumpjetLocomotorCLSID;

namespace Magnetron
{
	// Is this warhead's Locomotor= the jumpjet? Only then is it safe to treat
	// the victim's new locomotor as a JumpjetLocomotionClass and write its
	// flight fields -- doing that to a Drive/Teleport/Rocket locomotor would
	// scribble over unrelated members.
	bool WarheadUsesJumpjet(WarheadTypeClass* pWH);

	// Called at the ENTRY of TechnoClass::ImbueLocomotor -- the one funnel
	// both the vanilla call site and Phobos's full-replacement C++ call pass
	// through. Registers the hold; the flight overrides are applied on the
	// next PerFrame tick, by which point the new locomotor exists.
	//
	// The warhead is not an argument: ImbueLocomotor only receives a CLSID.
	// It is resolved from the firer's own weapons instead -- see
	// FindLocomotorWarhead in the .cpp.
	void OnImbued(TechnoClass* pFirer, FootClass* pVictim);

	// Per-frame: releases victims whose beam has stopped or whose hold has
	// outlived Magnetron.MaxHoldTime. Cheap when idle (no active holds).
	void PerFrame();
}
