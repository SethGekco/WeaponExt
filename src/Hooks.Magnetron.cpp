#include "Magnetron.h"

#include <TechnoClass.h>
// TechnoClass.h -> Helpers/Cast.h needs FootClass complete.
#include <FootClass.h>
#include <BulletClass.h>

#include <Utilities/Macro.h>

// ---------------------------------------------------------------------------
// The post-imbue seat: 0x469700, the instruction immediately after
// `call TechnoClass::ImbueLocomotor` (0x4696FB) in BulletClass::Logics.
//
// ⚠ The stolen bytes here are `E9 9F 03 00 00` -- a 5-byte RELATIVE jmp to
// 0x469AA4. Returning 0 would make Syringe's trampoline re-execute an
// un-relocated branch and jump into garbage; that is the documented crash in
// the Encyclopedia's Target-Evaluation-Threat.md / Syringe-Stub-Semantics.md.
// So this handler replicates the jump by returning the destination
// explicitly, and must NEVER `return 0`.
//
// Register state verified by disassembly: ImbueLocomotor's epilogue at
// 0x710405 pops edi/esi/ebp/ebx, so the caller's registers survive the call.
// ESI = the bullet (set long before), EDI = the victim FootClass* (loaded at
// 0x469648). The firer is bullet->Owner, the warhead is bullet->WH.
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x469700, BulletClass_Logics_MagnetronImbued, 0x5)
{
	GET(BulletClass* const, pBullet, ESI);
	GET(FootClass* const, pVictim, EDI);

	if (pBullet && pVictim)
		Magnetron::OnImbued(pBullet->Owner, pVictim, pBullet->WH);

	return 0x469AA4;   // replicate the stolen jmp; never 0
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
