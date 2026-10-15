#include "WeaponExtDiag.h"

#include <SpawnManagerClass.h>
#include <TechnoClass.h>
// TechnoClass.h -> Helpers/Cast.h instantiates generic_cast<const FootClass*>.
#include <FootClass.h>
#include <Fundamentals.h>

#include <Ext/TechnoType/Body.h>

#include <Utilities/Macro.h>

// ---------------------------------------------------------------------------
// Spawner auto-deploy (DESIGN.md 7.3b).
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
// ⚠ Bypassing a null-target check is only safe because the downstream
// Preparing(2) handler guards it: 0x6B765C tests Target and has a real
// no-target fall-through. Checked before this was written.
//
// ⚠⚠ WHY THE WAVE LATCH EXISTS. The AI loop starts at `xor ebx,ebx` (0x6B7271)
// and indexes `[edx+ebx*4]`, i.e. it walks nodes from 0 upward, and the
// manager permits only ONE launch at a time (Status Launching/CoolDown). So
// the naive version of this hook let node 0 launch, come home (no target),
// re-dock, and immediately win the single launch slot again -- forever.
// Nodes 1 and 2 starved, and a real attack order had to fight node 0 for the
// slot. Rex saw exactly that: "only the first one deploys... the last two
// kinda struggle following orders... almost like they struggle synchronising".
//
// The fix is to treat a deployment as a WAVE: once the gate opens, keep it
// open for the remaining docked nodes so the engine's own one-at-a-time pacing
// walks the whole wing out, then close it and refuse to start another wave
// until `Spawner.AutoDeploy.Delay` frames have passed.
// ---------------------------------------------------------------------------

namespace
{
	// Per-manager wave state. Fixed table, no allocation; entries are
	// validated against SpawnManagerClass::Array before use so a destroyed
	// carrier cannot leave us holding a dangling pointer.
	constexpr int MaxWaves = 16;

	struct Wave
	{
		SpawnManagerClass* Mgr;
		int Pending;         // launches still owed in the current wave
		int NextWaveFrame;   // earliest frame a new wave may begin
	};

	Wave Waves[MaxWaves] = {};
	int WaveCount = 0;

	bool StillAlive(SpawnManagerClass* pMgr)
	{
		for (auto const pItem : SpawnManagerClass::Array)
		{
			if (pItem == pMgr)
				return true;
		}

		return false;
	}

	void ReapDeadWaves()
	{
		for (int i = WaveCount - 1; i >= 0; --i)
		{
			if (!Waves[i].Mgr || !StillAlive(Waves[i].Mgr))
			{
				Waves[i] = Waves[WaveCount - 1];
				Waves[WaveCount - 1] = Wave {};
				--WaveCount;
			}
		}
	}

	Wave* GetWave(SpawnManagerClass* pMgr)
	{
		for (int i = 0; i < WaveCount; ++i)
		{
			if (Waves[i].Mgr == pMgr)
				return &Waves[i];
		}

		if (WaveCount >= MaxWaves)
		{
			ReapDeadWaves();
			if (WaveCount >= MaxWaves)
				return nullptr;   // genuinely saturated; fall back to vanilla
		}

		Waves[WaveCount] = Wave { pMgr, 0, 0 };
		return &Waves[WaveCount++];
	}
}

DEFINE_HOOK(0x6B72A2, SpawnManagerClass_AI_AutoDeploy, 0x5)
{
	enum { LaunchAnyway = 0x6B72AD, LetVanillaDecide = 0 };

	GET(SpawnManagerClass* const, pThis, ESI);

	if (!pThis)
		return LetVanillaDecide;

	auto const pOwner = pThis->Owner;
	if (!pOwner)
		return LetVanillaDecide;

	auto const pType = pOwner->GetTechnoType();
	if (!pType)
		return LetVanillaDecide;

	auto const pExt = TechnoTypeExt::ExtMap.Find(pType);
	if (!pExt || !pExt->Spawner_AutoDeploy)
		return LetVanillaDecide;   // vanilla behaviour, byte for byte

	auto const pWave = GetWave(pThis);
	if (!pWave)
		return LetVanillaDecide;

	// A real attack order outranks us completely. Abandon any wave in
	// progress so we stop competing for the single launch slot -- that
	// competition is what made ordered attacks desynchronise.
	if (pThis->Target)
	{
		pWave->Pending = 0;
		return LetVanillaDecide;
	}

	// Mid-wave: keep the gate open so the rest of the wing follows out on the
	// engine's own pacing instead of node 0 monopolising the slot.
	if (pWave->Pending > 0)
	{
		--pWave->Pending;

		if (pWave->Pending == 0)
		{
			pWave->NextWaveFrame = Unsorted::CurrentFrame
				+ pExt->Spawner_AutoDeploy_Delay;
		}

		return LaunchAnyway;
	}

	// Between waves.
	if (Unsorted::CurrentFrame < pWave->NextWaveFrame)
		return LetVanillaDecide;

	const int docked = pThis->CountDockedSpawns();
	if (docked < pExt->Spawner_AutoDeploy_MinReady)
		return LetVanillaDecide;   // wait until enough are home and reloaded

	// Start a wave: this launch, plus one for each other docked node.
	pWave->Pending = docked - 1;

	if (pWave->Pending == 0)
	{
		pWave->NextWaveFrame = Unsorted::CurrentFrame
			+ pExt->Spawner_AutoDeploy_Delay;
	}

	WeaponDiag::SpawnerLine(pType->ID, docked, pThis->CountAliveSpawns());

	return LaunchAnyway;
}
