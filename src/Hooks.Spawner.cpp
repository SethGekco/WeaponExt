#include "WeaponExtDiag.h"

#include <SpawnManagerClass.h>
#include <TechnoClass.h>
// TechnoClass.h -> Helpers/Cast.h instantiates generic_cast<const FootClass*>.
#include <FootClass.h>

#include <Ext/TechnoType/Body.h>

#include <Utilities/Macro.h>

// ---------------------------------------------------------------------------
// Spawner auto-deploy (DESIGN.md 7.3b, phase SP1).
//
// `SpawnManagerClass::AI` dispatches each node through a jump table at
// 0x6B7B6C indexed by SpawnNodeStatus. The Idle (docked) handler begins:
//
//   6b72a2  mov  eax,[esi+0x68]   ; SpawnManagerClass::Target
//   6b72a5  test eax,eax
//   6b72a7  je   0x6b795a         ; no target -> this node stays docked
//           ... SpawnTimer must have expired ...
//   6b72ce  cmp  [esi+0x70],0x2   ; manager Status == CoolDown -> skip
//
// That first `je` is the entire reason a Carrier sits on its Hornets until
// ordered to attack. Returning 0x6B72AD skips it and leaves the other two
// gates intact, so SpawnReloadRate pacing and the cooldown still apply.
//
// Stolen bytes are the `mov` + `test` -- 5 bytes, two whole instructions, no
// relative branch, so returning 0 is safe for the pass-through case.
//
// ⚠ Bypassing a null-target check is only safe because the downstream
// Preparing(2) handler guards it: 0x6B765C does `mov eax,[esi+0x68]` then
// `test/jne`, with a real fall-through branch for the no-target case. Checked
// before writing this, because a null deref here would be worse than the
// feature being absent.
//
// Unhooked by Phobos, Ares, Antares and Kratos.
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x6B72A2, SpawnManagerClass_AI_AutoDeploy, 0x5)
{
	enum { LaunchAnyway = 0x6B72AD, LetVanillaDecide = 0 };

	GET(SpawnManagerClass* const, pThis, ESI);

	if (!pThis || pThis->Target)
		return LetVanillaDecide;   // has a target: ordinary behaviour

	auto const pOwner = pThis->Owner;
	if (!pOwner)
		return LetVanillaDecide;

	auto const pType = pOwner->GetTechnoType();
	if (!pType)
		return LetVanillaDecide;

	auto const pExt = TechnoTypeExt::ExtMap.Find(pType);
	if (!pExt || !pExt->Spawner_AutoDeploy)
		return LetVanillaDecide;   // vanilla behaviour, byte for byte

	// Hold until enough are actually home and ready, so a carrier can be set
	// to launch its whole wing at once rather than dribbling them out.
	if (pThis->CountDockedSpawns() < pExt->Spawner_AutoDeploy_MinReady)
		return LetVanillaDecide;

	WeaponDiag::SpawnerLine(pType->ID,
		pThis->CountDockedSpawns(), pThis->CountAliveSpawns());

	return LaunchAnyway;
}
