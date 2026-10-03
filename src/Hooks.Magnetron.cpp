#include "Magnetron.h"

#include <TechnoClass.h>
// TechnoClass.h -> Helpers/Cast.h needs FootClass complete.
#include <FootClass.h>

#include <Utilities/Macro.h>

// ---------------------------------------------------------------------------
// The imbue funnel: `TechnoClass::ImbueLocomotor` itself, at its entry.
//
// ⚠ WHY NOT THE BULLET PATH. The first version of this hooked 0x469700, the
// instruction right after the vanilla `call ImbueLocomotor` in
// BulletClass::Logics. It never ran once. **Phobos's handler at 0x4696CE is a
// FULL REPLACEMENT**: it calls `ImbueLocomotor` itself from C++ and then
// `return 0x469AA4`, jumping clean past 0x4696FB and 0x469700. The seat was
// legal (no registry overlap, perfect geometry) but dead -- exactly the
// "legal is not live" / full-replacement trap in _TRAPS-READ-FIRST.md, and
// invisible to the overlap and bounds checkers.
//
// The function entry is the one place BOTH paths must pass through: vanilla's
// single call site and Phobos's C++ call. Stolen bytes are
// `83 EC 1C | 53 | 55` -- sub esp,0x1c + push ebx + push ebp, exactly 5, one
// whole instruction each and no relative branch, so `return 0` is safe.
//
// At the entry, nothing has been pushed yet: ECX = the firer (thiscall) and
// [esp+0x4] = the victim argument. The CLSID argument is NOT read here -- we
// get the locomotor identity from the warhead instead, which is equivalent
// and simpler.
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x710000, TechnoClass_ImbueLocomotor_Magnetron, 0x5)
{
	GET(TechnoClass* const, pFirer, ECX);
	GET_STACK(FootClass* const, pVictim, 0x4);

	if (pFirer && pVictim)
		Magnetron::OnImbued(pFirer, pVictim);

	return 0;
}

// ---------------------------------------------------------------------------
// Per-frame supervision, needed because vanilla has no beam-stop detection of
// any kind. 0x55B6B3 is LogicClass::AI immediately after the object-update
// loop; the Encyclopedia documents it as uncontended and the stolen bytes are
// one whole `mov ecx,imm32` with no relative branch, so `return 0` is safe.
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x55B6B3, LogicClass_AI_MagnetronPerFrame, 0x5)
{
	Magnetron::PerFrame();

	return 0;
}
