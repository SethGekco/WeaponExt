// Section 9: warhead size multiplier (country source; step 1).
//
// CellSpread is a field on the shared WarheadTypeClass, so there is nothing
// per-house to scale. Instead: scale it on the way into BulletClass::Detonate
// and put it back on the way out, so everything inside the detonation --
// vanilla MapClass::DamageArea included -- sees the scaled radius.
//
// Seats (both co-tenanted with Phobos, both return 0 there in the normal path;
// taken from Phobos src/Ext/Bullet/Hooks.DetonateLogics.cpp, not guessed):
//   0x4690C1 size 8  BulletClass_Logics_DetonateOnAllMapObjects  ESI = bullet
//   0x469AA4 size 5  BulletClass_Logics_Extras                   ESI = bullet
// Detonate has an EBP frame (Phobos reads coords at [ebp+0x8] inside it).
//
// Hazards, and how each is handled:
//  * Nesting. A detonation kills something whose death weapon detonates
//    inside the outer DamageArea. Frames form a stack; restores are LIFO.
//  * Same warhead nested. The inner frame must scale from the TRUE original,
//    not from the outer frame's already-scaled value -- so the base is taken
//    from the oldest live frame for that warhead.
//  * Early exits that skip the restore seat (e.g. Phobos returning
//    ReturnFromFunction at 0x4690C1 after we ran). A frame records the EBP of
//    its Detonate call; any later seat whose EBP is >= a recorded EBP proves
//    that call has already returned (a live caller sits HIGHER on the stack
//    than any callee), so the stale frame is unwound and its value restored.
//
// Zero-cost invariant: a house with no multiplier (or exactly 1.0) pushes no
// frame and writes nothing.

#include <BulletClass.h>
#include <WarheadTypeClass.h>
#include <TechnoClass.h>
// TechnoClass.h -> Helpers/Cast.h needs FootClass complete; see Hooks.FrameProbe.cpp.
#include <FootClass.h>
#include <HouseClass.h>
#include <HouseTypeClass.h>
#include <CCINIClass.h>

#include <Utilities/Macro.h>
#include <Utilities/Debug.h>
#include <Utilities/TemplateDef.h>

#include <Ext/WarheadType/Body.h>

#include <type_traits>
#include <unordered_map>
#include <utility>

namespace
{
	using SpreadT = std::remove_reference_t<decltype(std::declval<WarheadTypeClass&>().CellSpread)>;

	// ---- Country source --------------------------------------------------
	// Keys are only ever compared, never dereferenced, so a stale key after a
	// HouseType is destroyed is harmless.
	std::unordered_map<const HouseTypeClass*, double> CountryMultiplier;

	double MultiplierFor(const BulletClass* pBullet)
	{
		auto const pFirer = pBullet->Owner;
		if (!pFirer)
			return 1.0;   // step 1: firer dead before impact -> unscaled (9.6)

		auto const pHouse = pFirer->Owner;
		if (!pHouse || !pHouse->Type)
			return 1.0;

		auto const it = CountryMultiplier.find(pHouse->Type);
		return it == CountryMultiplier.end() ? 1.0 : it->second;
	}

	// ---- Scale/restore stack --------------------------------------------
	struct Frame
	{
		BulletClass* Bullet;
		WarheadTypeClass* Warhead;
		DWORD Ebp;
		SpreadT Previous;   // value to put back when this frame pops
	};

	// Nesting depth is bounded by how deep death-weapon chains recurse; 64 is
	// far past anything the engine's own stack survives.
	constexpr int MaxFrames = 64;
	Frame Frames[MaxFrames];
	int FrameCount = 0;

	void PopTop()
	{
		auto& f = Frames[--FrameCount];
		f.Warhead->CellSpread = f.Previous;
	}

	// Called on ENTRY to a Detonate: any frame at the same or a deeper stack
	// position (EBP <= ours) belongs to a Detonate call that has already
	// returned, because a still-running caller always sits higher up.
	void UnwindStale(DWORD ebp)
	{
		while (FrameCount > 0 && Frames[FrameCount - 1].Ebp <= ebp)
			PopTop();
	}

	SpreadT OriginalSpread(WarheadTypeClass* pWH)
	{
		for (int i = 0; i < FrameCount; ++i)
		{
			if (Frames[i].Warhead == pWH)
				return Frames[i].Previous;
		}
		return pWH->CellSpread;
	}
}

// Country tag, read from the country's own section. Same seats, registers and
// sizes as Phobos's HouseTypeExt LoadFromINI (src/Ext/HouseType/Body.cpp) --
// the function has two exits, hence DEFINE_HOOK_AGAIN.
DEFINE_HOOK_AGAIN(0x51215A, HouseTypeClass_LoadFromINI_WeaponExt, 0x5)
DEFINE_HOOK(0x51214F, HouseTypeClass_LoadFromINI_WeaponExt, 0x5)
{
	GET(HouseTypeClass*, pItem, EBX);
	GET_BASE(CCINIClass*, pINI, 0x8);

	if (!pItem || !pINI || !pINI->GetSection(pItem->ID))
		return 0;

	INI_EX exINI(pINI);
	Nullable<double> mult;
	mult.Read(exINI, pItem->ID, "WarheadSize.Multiplier");

	if (mult.isset())
	{
		double v = mult.Get();
		if (v < 0.0)
		{
			Debug::Log("[WeaponExt] %s: WarheadSize.Multiplier=%.2f is negative; using 0.\n",
				pItem->ID, v);
			v = 0.0;
		}

		if (v == 1.0)
			CountryMultiplier.erase(pItem);
		else
			CountryMultiplier[pItem] = v;

		Debug::Log("[WeaponExt] %s: WarheadSize.Multiplier=%.2f\n", pItem->ID, v);
	}

	return 0;
}

// Scale on the way in. 8 bytes as Phobos declares it; return 0 so the stolen
// bytes re-execute unchanged.
DEFINE_HOOK(0x4690C1, BulletClass_Detonate_WarheadSizeApply, 0x8)
{
	GET(BulletClass*, pThis, ESI);

	UnwindStale(R->EBP());

	if (!pThis || !pThis->WH)
		return 0;

	const double multiplier = MultiplierFor(pThis);
	if (multiplier == 1.0)
		return 0;

	auto const pWH = pThis->WH;
	auto const pExt = WarheadTypeExt::ExtMap.Find(pWH);
	if (pExt && pExt->WarheadSize_Exempt)
		return 0;

	if (FrameCount >= MaxFrames)
	{
		Debug::Log("[WeaponExt] WarheadSize: frame stack full, %s left unscaled.\n", pWH->ID);
		return 0;
	}

	const SpreadT original = OriginalSpread(pWH);
	const double scaled = pExt
		? pExt->ScaledSpread((double)original, multiplier)
		: ((double)original * multiplier);

	Frames[FrameCount++] = { pThis, pWH, R->EBP(), pWH->CellSpread };
	pWH->CellSpread = (SpreadT)scaled;

	return 0;
}

// Restore on the way out. Everything between the two seats -- including the
// vanilla area damage -- has run with the scaled radius by now.
DEFINE_HOOK(0x469AA4, BulletClass_Detonate_WarheadSizeRestore, 0x5)
{
	GET(BulletClass*, pThis, ESI);
	const DWORD ebp = R->EBP();

	// Anything nested deeper than this Detonate is finished; then pop this
	// call's own frame if it pushed one.
	while (FrameCount > 0 && Frames[FrameCount - 1].Ebp < ebp)
		PopTop();

	if (FrameCount > 0
		&& Frames[FrameCount - 1].Ebp == ebp
		&& Frames[FrameCount - 1].Bullet == pThis)
	{
		PopTop();
	}

	return 0;
}
