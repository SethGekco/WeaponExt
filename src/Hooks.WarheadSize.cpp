// Section 9: warhead size multiplier.
//   Step 1: country source, [CombatDamage]/per-warhead limits.
//   Step 2: WarheadSize.Attach timed source, fire-time capture, anim swap
//           (seats listed above each hook at the bottom of this file).
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
#include <AnimTypeClass.h>
#include <AnimClass.h>
#include <Unsorted.h>
#include <CellSpread.h>

#include <Utilities/Macro.h>
#include <Utilities/Debug.h>
#include <Utilities/TemplateDef.h>

#include <Ext/WarheadType/Body.h>

#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	using SpreadT = std::remove_reference_t<decltype(std::declval<WarheadTypeClass&>().CellSpread)>;

	// ---- Country source --------------------------------------------------
	// Keys are only ever compared, never dereferenced, so a stale key after a
	// HouseType is destroyed is harmless.
	std::unordered_map<const HouseTypeClass*, double> CountryMultiplier;

	double CountryFor(const HouseClass* pHouse)
	{
		if (!pHouse || !pHouse->Type)
			return 1.0;

		auto const it = CountryMultiplier.find(pHouse->Type);
		return it == CountryMultiplier.end() ? 1.0 : it->second;
	}

	// ---- Timed effect source (WarheadSize.Attach) --------------------------
	// Entries are erased in the TechnoClass DTOR hook, so a freed pointer can
	// never be reused by a new object while still carrying an old effect.
	// Expiry is checked lazily on read; no per-frame work.
	struct AttachState
	{
		double Multiplier;
		int UntilFrame;
	};
	std::unordered_map<const TechnoClass*, AttachState> Attached;

	double AttachedFor(const TechnoClass* pTechno)
	{
		if (!pTechno)
			return 1.0;

		auto const it = Attached.find(pTechno);
		if (it == Attached.end())
			return 1.0;

		if (Unsorted::CurrentFrame >= it->second.UntilFrame)
		{
			Attached.erase(it);
			return 1.0;
		}
		return it->second.Multiplier;
	}

	// Everything this firer's shots are scaled by right now. Sources multiply.
	double LiveMultiplier(const TechnoClass* pFirer)
	{
		if (!pFirer)
			return 1.0;
		return CountryFor(pFirer->Owner) * AttachedFor(pFirer);
	}

	// ---- Per-bullet state -------------------------------------------------
	// Captured at fire time (0x6FF660) so a shot keeps its multiplier and its
	// house even if the firer dies before impact; also remembers the effective
	// multiplier a detonation was scaled with, for the anim swap at 0x469C46.
	// Erased in the BulletClass DTOR hook.
	struct BulletState
	{
		bool Captured = false;
		double FireMultiplier = 1.0;
		HouseClass* FirerHouse = nullptr;
		double AppliedMultiplier = 1.0;   // 1.0 = this detonation was not scaled
	};
	std::unordered_map<const BulletClass*, BulletState> Bullets;

	double MultiplierFor(const BulletClass* pBullet)
	{
		auto const it = Bullets.find(pBullet);
		if (it != Bullets.end() && it->second.Captured)
			return it->second.FireMultiplier;
		return LiveMultiplier(pBullet->Owner);
	}

	// The house that fired, as of fire time when we captured it (a firer that
	// changes owner mid-flight does not change whose shot it was).
	HouseClass* FirerHouseFor(const BulletClass* pBullet)
	{
		auto const it = Bullets.find(pBullet);
		if (it != Bullets.end() && it->second.FirerHouse)
			return it->second.FirerHouse;

		return pBullet->Owner ? pBullet->Owner->Owner : nullptr;
	}

	// ---- Universal limits ([CombatDamage]) --------------------------------
	// Unset = no limit. Each can be overridden per warhead with the same key.
	struct GlobalLimits
	{
		Nullable<double> IgnoreSpreadBelow;
		Nullable<double> IgnoreSpreadAbove;
		Nullable<double> MultiplierCap;
		Nullable<double> MultiplierFloor;
		Nullable<double> SpreadCap;
		Nullable<double> SpreadFloor;
		Nullable<bool> Overflow;           // step 3; default yes
		Nullable<int> EngineSpreadLimit;   // step 3; overrides detection
	} Global;

	// ---- Engine spread limit (step 3) -------------------------------------
	// MapClass::DamageArea looks CellSpread up in the engine's cell-count
	// table (CellSpread::NumCells, 0x7ED3D0). Its length is not documented
	// anywhere we can check, so it is MEASURED: entry n must equal the number
	// of cell offsets within distance n under the engine's own metric
	// (CellSpread::GetDistance: longer axis + half the shorter). The last n
	// that matches is the limit. Reading past the table is a read of the
	// game's own static data, never a write. -1 = could not establish one.
	int ExpectedCells(int n)
	{
		int count = 0;
		for (int dx = -n; dx <= n; ++dx)
		{
			for (int dy = -n; dy <= n; ++dy)
			{
				if (CellSpread::GetDistance(dx, dy) <= (size_t)n)
					++count;
			}
		}
		return count;
	}

	int EngineLimit()
	{
		if (Global.EngineSpreadLimit.isset())
			return Global.EngineSpreadLimit.Get();

		static int limit = -2;   // -2 = not measured yet
		if (limit != -2)
			return limit;

		constexpr int MaxProbe = 32;
		limit = -1;
		for (int n = 0; n <= MaxProbe; ++n)
		{
			if ((int)CellSpread::NumCells((unsigned int)n) != ExpectedCells(n))
				break;
			limit = n;
		}

		Debug::Log("[WeaponExt] WarheadSize: engine spread table (0x7ED3D0) raw entries 0-16:");
		for (int n = 0; n <= 16; ++n)
			Debug::Log(" %u", (unsigned int)CellSpread::NumCells((unsigned int)n));
		Debug::Log("\n");

		if (limit < 0)
			Debug::Log("[WeaponExt] WarheadSize: could NOT establish the engine spread limit; "
				"scaled warheads will not grow past their own CellSpread (the overflow pass "
				"covers the rest). Set [CombatDamage] WarheadSize.EngineSpreadLimit= to override.\n");
		else
			Debug::Log("[WeaponExt] WarheadSize: engine spread limit measured as %d cells.\n", limit);

		return limit;
	}

	// Warhead value if set, else the global, else "no limit" (returns false).
	bool Resolve(const Nullable<double>* pLocal, const Nullable<double>& global, double& out)
	{
		if (pLocal && pLocal->isset())
		{
			out = pLocal->Get();
			return true;
		}
		if (global.isset())
		{
			out = global.Get();
			return true;
		}
		return false;
	}

	// The CellSpread this warhead should detonate with, or a negative value
	// for "leave it alone". Pure function of INI data + the multiplier, so
	// every client computes the same value -- required, because CellSpread
	// decides who is damaged. *pEffective receives the multiplier after the
	// Cap/Floor clamp (what the anim swap threshold compares against).
	double ScaledSpread(const WarheadTypeExt::ExtData* pExt, double original, double multiplier,
		double* pEffective)
	{
		auto L = [pExt](Nullable<double> WarheadTypeExt::ExtData::* m) -> const Nullable<double>*
			{ return pExt ? &(pExt->*m) : nullptr; };
		double v;

		// 1. Clamp the multiplier itself.
		if (Resolve(L(&WarheadTypeExt::ExtData::WarheadSize_MultiplierCap), Global.MultiplierCap, v)
			&& multiplier > v)
			multiplier = v;
		if (Resolve(L(&WarheadTypeExt::ExtData::WarheadSize_MultiplierFloor), Global.MultiplierFloor, v)
			&& multiplier < v)
			multiplier = v;
		if (multiplier < 0.0)
			multiplier = 0.0;
		if (multiplier == 1.0)
			return -1.0;
		*pEffective = multiplier;

		// 2. The spread we scale from (FromZero stands in for CellSpread=0).
		double base = original;
		if (base <= 0.0)
		{
			if (!pExt || pExt->WarheadSize_FromZero <= 0.0)
				return -1.0;
			base = pExt->WarheadSize_FromZero;
		}

		// 3. Filter: warheads outside the eligible band are never touched.
		if (Resolve(L(&WarheadTypeExt::ExtData::WarheadSize_IgnoreSpreadBelow), Global.IgnoreSpreadBelow, v)
			&& base < v)
			return -1.0;
		if (Resolve(L(&WarheadTypeExt::ExtData::WarheadSize_IgnoreSpreadAbove), Global.IgnoreSpreadAbove, v)
			&& base > v)
			return -1.0;

		// 4. Scale, then limit the result. Limits only ever stop the change
		// partway -- enlarging never ends smaller than the base because the
		// cap sits below it, and shrinking never ends larger because the floor
		// sits above it.
		double scaled = base * multiplier;
		if (multiplier > 1.0
			&& Resolve(L(&WarheadTypeExt::ExtData::WarheadSize_SpreadCap), Global.SpreadCap, v)
			&& scaled > v)
			scaled = v > base ? v : base;
		if (multiplier < 1.0
			&& Resolve(L(&WarheadTypeExt::ExtData::WarheadSize_SpreadFloor), Global.SpreadFloor, v)
			&& scaled < v)
			scaled = v < base ? v : base;

		return scaled;
	}

	// ---- Scale/restore stack --------------------------------------------
	struct Frame
	{
		BulletClass* Bullet;
		WarheadTypeClass* Warhead;
		DWORD Ebp;
		SpreadT Previous;   // value to put back when this frame pops
		double FullSpread;  // the scaled spread before the engine-limit clamp
		bool Overflow;      // FullSpread > what was written, ring pass wanted
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

	bool IsTechno(AbstractClass* pAbs)
	{
		if (!pAbs)
			return false;

		switch (pAbs->WhatAmI())
		{
		case AbstractType::Unit:
		case AbstractType::Infantry:
		case AbstractType::Building:
		case AbstractType::Aircraft:
			return true;
		default:
			return false;
		}
	}

	bool HouseAllowed(int mask, HouseClass* pFirer, HouseClass* pVictim)
	{
		if ((mask & WarheadSizeHouse_All) == WarheadSizeHouse_All)
			return true;
		if (!pFirer || !pVictim)
			return false;
		if (pFirer == pVictim)
			return (mask & WarheadSizeHouse_Owner) != 0;
		if (pFirer->IsAlliedWith(pVictim))
			return (mask & WarheadSizeHouse_Allies) != 0;
		return (mask & WarheadSizeHouse_Enemies) != 0;
	}

	void Attach(TechnoClass* pTechno, double multiplier, int untilFrame)
	{
		// Re-applying replaces the value and restarts the timer (no stacking).
		Attached[pTechno] = { multiplier, untilFrame };
	}

	// WarheadSize.Attach: give every eligible techno inside this detonation's
	// spread the timed multiplier. spreadCells is the FULL scaled spread
	// (past the engine limit too), so a scaled detonation buffs its whole area.
	// Iteration is over the engine's own TechnoClass::Array in its order and
	// only writes our map -- deterministic on every client.
	void ApplyAttach(BulletClass* pBullet, const CoordStruct& where, double spreadCells)
	{
		auto const pWH = pBullet->WH;
		auto const pExt = pWH ? WarheadTypeExt::ExtMap.Find(pWH) : nullptr;
		if (!pExt || !pExt->HasAttach())
			return;

		const double multiplier = pExt->WarheadSize_Attach.Get() < 0.0 ? 0.0 : pExt->WarheadSize_Attach.Get();
		const int untilFrame = Unsorted::CurrentFrame + pExt->WarheadSize_Attach_Duration;
		const int houses = pExt->WarheadSize_Attach_Houses;
		HouseClass* const pFirerHouse = FirerHouseFor(pBullet);

		auto eligible = [&](TechnoClass* pTechno)
			{
				return pTechno && pTechno->IsAlive && !pTechno->InLimbo && pTechno->IsOnMap
					&& HouseAllowed(houses, pFirerHouse, pTechno->Owner);
			};

		const double radius = spreadCells * 256.0;   // leptons
		if (radius <= 0.0)
		{
			// No spread: only the thing that was actually hit.
			if (IsTechno(pBullet->Target))
			{
				auto const pTarget = static_cast<TechnoClass*>(pBullet->Target);
				if (eligible(pTarget))
					Attach(pTarget, multiplier, untilFrame);
			}
			return;
		}

		const double radiusSq = radius * radius;
		for (int i = 0; i < TechnoClass::Array.Count; ++i)
		{
			auto const pTechno = TechnoClass::Array.Items[i];
			if (!eligible(pTechno))
				continue;

			const CoordStruct c = pTechno->GetCoords();
			const double dx = (double)c.X - where.X;
			const double dy = (double)c.Y - where.Y;
			const double dz = (double)c.Z - where.Z;
			if (dx * dx + dy * dy + dz * dz <= radiusSq)
				Attach(pTechno, multiplier, untilFrame);
		}
	}

	bool OverflowEnabled(const WarheadTypeExt::ExtData* pExt)
	{
		if (pExt && pExt->WarheadSize_Overflow.isset())
			return pExt->WarheadSize_Overflow.Get();
		return Global.Overflow.Get(true);
	}

	// Step 3: damage the ring the engine could not reach -- technos farther
	// than innerCells and no farther than outerCells from the detonation.
	// Runs while CellSpread still holds innerCells (the clamped value), and
	// passes the inner edge as the distance, so ReceiveDamage's own falloff
	// gives every ring victim the warhead's edge damage (PercentAtMax) with
	// the normal Verses / armor / ownership rules.
	//
	// Victims are snapshotted first (ReceiveDamage can kill, which reshapes
	// TechnoClass::Array and can nest further detonations), then each is
	// re-checked for IsAlive right before it is hit -- the same pattern
	// Phobos uses for its own area effects. Order is the engine's array
	// order: deterministic on every client.
	void ApplyOverflow(BulletClass* pBullet, const CoordStruct& where,
		double innerCells, double outerCells)
	{
		auto const pWH = pBullet->WH;
		const int damage = pBullet->Health;   // bullets carry their damage in Health
		if (!pWH || damage == 0 || outerCells <= innerCells)
			return;

		const double inner = innerCells * 256.0;
		const double outer = outerCells * 256.0;
		const double innerSq = inner * inner;
		const double outerSq = outer * outer;

		std::vector<TechnoClass*> victims;
		for (int i = 0; i < TechnoClass::Array.Count; ++i)
		{
			auto const pTechno = TechnoClass::Array.Items[i];
			if (!pTechno || !pTechno->IsAlive || pTechno->InLimbo || !pTechno->IsOnMap)
				continue;

			const CoordStruct c = pTechno->GetCoords();
			const double dx = (double)c.X - where.X;
			const double dy = (double)c.Y - where.Y;
			const double dz = (double)c.Z - where.Z;
			const double dSq = dx * dx + dy * dy + dz * dz;
			if (dSq > innerSq && dSq <= outerSq)
				victims.push_back(pTechno);
		}

		TechnoClass* const pAttacker = pBullet->Owner;
		HouseClass* const pHouse = FirerHouseFor(pBullet);
		const int edge = (int)inner;

		for (auto const pVictim : victims)
		{
			if (!pVictim->IsAlive)
				continue;

			int dealt = damage;
			pVictim->ReceiveDamage(&dealt, edge, pWH, pAttacker, false, false, pHouse);
		}
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

// Universal limits from [CombatDamage]. Same seat, register and stack offset as
// Phobos's RulesData_LoadBeforeTypeData (src/Ext/Rules/Body.cpp). Runs once
// per INI (rules, then map), and Read() only overwrites when the key is
// present, so a map can override the rules value.
DEFINE_HOOK(0x679A15, RulesData_LoadBeforeTypeData_WeaponExt, 0x6)
{
	GET_STACK(CCINIClass*, pINI, 0x4);

	if (!pINI)
		return 0;

	constexpr const char* section = "CombatDamage";
	INI_EX exINI(pINI);

	Global.IgnoreSpreadBelow.Read(exINI, section, "WarheadSize.IgnoreSpreadBelow");
	Global.IgnoreSpreadAbove.Read(exINI, section, "WarheadSize.IgnoreSpreadAbove");
	Global.MultiplierCap.Read(exINI, section, "WarheadSize.MultiplierCap");
	Global.MultiplierFloor.Read(exINI, section, "WarheadSize.MultiplierFloor");
	Global.SpreadCap.Read(exINI, section, "WarheadSize.SpreadCap");
	Global.SpreadFloor.Read(exINI, section, "WarheadSize.SpreadFloor");
	Global.Overflow.Read(exINI, section, "WarheadSize.Overflow");
	Global.EngineSpreadLimit.Read(exINI, section, "WarheadSize.EngineSpreadLimit");

	Debug::Log("[WeaponExt] [CombatDamage] WarheadSize ignoreBelow=%.2f ignoreAbove=%.2f "
		"multCap=%.2f multFloor=%.2f spreadCap=%.2f spreadFloor=%.2f (-1 = unset)\n",
		Global.IgnoreSpreadBelow.Get(-1.0), Global.IgnoreSpreadAbove.Get(-1.0),
		Global.MultiplierCap.Get(-1.0), Global.MultiplierFloor.Get(-1.0),
		Global.SpreadCap.Get(-1.0), Global.SpreadFloor.Get(-1.0));

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
	double effective = 1.0;
	const double scaled = ScaledSpread(pExt, (double)original, multiplier, &effective);
	if (scaled < 0.0)
		return 0;

	// Step 3: never hand the engine more spread than its table covers. The
	// ceiling is the measured limit, but never below the warhead's own
	// CellSpread (a modder's existing value is left exactly as vanilla runs
	// it). With no measurable limit, the ceiling IS the warhead's own value.
	const int limit = EngineLimit();
	const double own = (double)original;
	const double ceiling = limit >= 0 ? ((double)limit > own ? (double)limit : own) : own;
	const double written = scaled > ceiling ? ceiling : scaled;
	const bool overflow = scaled > written && OverflowEnabled(pExt);

	Frames[FrameCount++] = { pThis, pWH, R->EBP(), pWH->CellSpread, overflow ? scaled : written, overflow };
	pWH->CellSpread = (SpreadT)written;

	// Remembered for the anim swap / anim scale at 0x469C46, which runs after
	// the restore.
	if (pExt && pExt->WantsAnimHandling())
		Bullets[pThis].AppliedMultiplier = effective;

	return 0;
}

// Restore on the way out. Everything between the two seats -- including the
// vanilla area damage -- has run with the scaled radius by now.
DEFINE_HOOK(0x469AA4, BulletClass_Detonate_WarheadSizeRestore, 0x5)
{
	GET(BulletClass*, pThis, ESI);
	GET_BASE(CoordStruct const* const, pCoords, 0x8);   // as Phobos reads it here
	const DWORD ebp = R->EBP();

	// Anything nested deeper than this Detonate is finished.
	while (FrameCount > 0 && Frames[FrameCount - 1].Ebp < ebp)
		PopTop();

	const bool ownFrame = FrameCount > 0
		&& Frames[FrameCount - 1].Ebp == ebp
		&& Frames[FrameCount - 1].Bullet == pThis;

	// Both run before our own frame pops: the overflow ring needs CellSpread
	// still at the clamped value (its falloff edge), and Attach covers the
	// full scaled area.
	if (pThis && pCoords)
	{
		if (ownFrame)
		{
			// Copy first: the ring pass can nest detonations that push and
			// pop frames above ours.
			const Frame f = Frames[FrameCount - 1];
			if (f.Overflow)
				ApplyOverflow(pThis, *pCoords, (double)f.Warhead->CellSpread, f.FullSpread);
			ApplyAttach(pThis, *pCoords, f.FullSpread);
		}
		else if (pThis->WH)
		{
			ApplyAttach(pThis, *pCoords, (double)pThis->WH->CellSpread);
		}
	}

	// Nested detonations from the ring pass may have left deeper frames.
	while (FrameCount > 0 && Frames[FrameCount - 1].Ebp < ebp)
		PopTop();

	if (ownFrame
		&& FrameCount > 0
		&& Frames[FrameCount - 1].Ebp == ebp
		&& Frames[FrameCount - 1].Bullet == pThis)
	{
		PopTop();
	}

	return 0;
}

// ---------------------------------------------------------------------------
// Step 2 seats. All co-tenanted with Phobos at the same size; each Phobos
// handler named here returns 0 on its normal path unless noted.
// ---------------------------------------------------------------------------

// Fire-time capture. Phobos TechnoClass_FireAt_LateLogic
// (src/Ext/Techno/Hooks.Firing.cpp): ESI = firer, the freshly created bullet
// at STACK_OFFSET(0xB0, -0x74) = [esp+0x3C]. Returns 0.
//
// Every shot is recorded, including ×1.0 ones: otherwise a unit that fires
// unbuffed and gains WarheadSize.Attach mid-flight would have that shot fall
// back to the live lookup and land scaled. Cost is one map insert per shot
// and one erase in the DTOR; no game state is written.
DEFINE_HOOK(0x6FF660, TechnoClass_FireAt_WarheadSizeCapture, 0x6)
{
	GET(TechnoClass* const, pThis, ESI);
	auto const pBullet = R->Stack<BulletClass*>(0x3C);

	if (!pThis || !pBullet)
		return 0;

	auto& state = Bullets[pBullet];
	state.Captured = true;
	state.FireMultiplier = LiveMultiplier(pThis);
	state.FirerHouse = pThis->Owner;

	return 0;
}

// Phobos BulletClass_DTOR (src/Ext/Bullet/Body.cpp): ESI = bullet. Returns 0.
DEFINE_HOOK(0x4665E9, BulletClass_DTOR_WarheadSize, 0xA)
{
	GET(BulletClass*, pItem, ESI);

	Bullets.erase(pItem);

	return 0;
}

// Phobos TechnoClass_DTOR (src/Ext/Techno/Body.cpp): ECX = techno. Returns 0.
DEFINE_HOOK(0x6F4500, TechnoClass_DTOR_WarheadSize, 0x5)
{
	GET(TechnoClass*, pItem, ECX);

	Attached.erase(pItem);

	return 0;
}

// Bigger explosion art. Phobos BulletClass_Logics_DamageAnimSelected: the
// engine has just picked the damage anim into EBX, ESI = bullet. We swap EBX
// and return 0 so whoever runs next (Phobos, or vanilla) creates our anim.
//
// !! Phobos's handler here ALWAYS returns SkipGameCode (0x469C98). Syringe
// stops the chain at the first non-zero return, so this only works when
// WeaponExt.dll is listed BEFORE Phobos in the Syringe -i= order. If Phobos
// runs first we are never called and the vanilla anim plays -- no crash,
// just no swap. Also not covered: Phobos AnimList.CreateAll (reads AnimList
// directly, ignoring EBX) and SplashList anims (not in AnimList).
namespace
{
	// Step 2 swap. Returns the anim type that should be created (the engine's
	// pick when there is nothing to swap).
	AnimTypeClass* SwapAnim(const WarheadTypeExt::ExtData* pExt, WarheadTypeClass* pWH,
		AnimTypeClass* pAnimType, double m)
	{
		if (pExt->WarheadSize_AnimList_Scaled.empty())
			return pAnimType;

		const bool passed = pExt->WarheadSize_AnimList_Threshold.isset()
			? m >= pExt->WarheadSize_AnimList_Threshold.Get()
			: m > 1.0;
		if (!passed)
			return pAnimType;

		// Position of the engine's pick in the warhead's own AnimList.
		auto const& list = pWH->AnimList;
		int index = -1;
		for (int i = 0; i < list.Count; ++i)
		{
			if (list.Items[i] == pAnimType)
			{
				index = i;
				break;
			}
		}
		if (index < 0)
			return pAnimType;   // not from AnimList (e.g. a splash) -- leave it alone

		auto const& scaled = pExt->WarheadSize_AnimList_Scaled;
		const int last = (int)scaled.size() - 1;
		auto const pNew = scaled[index < last ? index : last];
		return pNew ? pNew : pAnimType;
	}

	// ---- Step 4a: per-anim draw scale -------------------------------------
	// The anim is created right after 0x469C46 (by Phobos's handler, or by
	// vanilla), so the scale is handed over through a one-shot "pending"
	// slot that the next AnimClass CTOR consumes -- but only if that anim has
	// the expected type and is built in the same frame, so a pending scale
	// can never leak onto some unrelated anim later. Overwritten by the next
	// detonation either way.
	struct PendingScale
	{
		const AnimTypeClass* Type = nullptr;
		double Scale = 1.0;
		int Frame = -1;
	} Pending;

	std::unordered_map<const AnimClass*, double> AnimScales;
}

DEFINE_HOOK(0x469C46, BulletClass_Detonate_WarheadSizeAnim, 0x8)
{
	GET(BulletClass*, pThis, ESI);
	GET(AnimTypeClass*, pAnimType, EBX);

	Pending.Type = nullptr;   // a stale handoff never outlives the next detonation

	if (!pThis || !pAnimType || !pThis->WH)
		return 0;

	auto const it = Bullets.find(pThis);
	if (it == Bullets.end() || it->second.AppliedMultiplier == 1.0)
		return 0;

	auto const pExt = WarheadTypeExt::ExtMap.Find(pThis->WH);
	if (!pExt || !pExt->WantsAnimHandling())
		return 0;

	const double m = it->second.AppliedMultiplier;

	auto const pFinal = SwapAnim(pExt, pThis->WH, pAnimType, m);
	if (pFinal != pAnimType)
		R->EBX(reinterpret_cast<DWORD>(pFinal));

	if (pExt->WarheadSize_AnimScale)
	{
		double scale = m;
		if (pExt->WarheadSize_AnimScale_Max.isset() && scale > pExt->WarheadSize_AnimScale_Max.Get())
			scale = pExt->WarheadSize_AnimScale_Max.Get();
		if (scale > 0.0 && scale != 1.0)
			Pending = { pFinal, scale, Unsorted::CurrentFrame };
	}

	return 0;
}

// Phobos AnimClass_CTOR (src/Ext/Anim/Body.cpp): ESI = anim, Type already
// set (Phobos reads pItem->Type here). Returns 0.
DEFINE_HOOK(0x4226F6, AnimClass_CTOR_WarheadSize, 0x6)
{
	GET(AnimClass*, pItem, ESI);

	if (Pending.Type && pItem && pItem->Type == Pending.Type
		&& Pending.Frame == Unsorted::CurrentFrame)
	{
		AnimScales[pItem] = Pending.Scale;
		Pending.Type = nullptr;   // one-shot
	}

	return 0;
}

// Phobos AnimClass_DTOR (src/Ext/Anim/Body.cpp): ESI = anim. Returns 0.
DEFINE_HOOK(0x422967, AnimClass_DTOR_WarheadSize, 0x6)
{
	GET(AnimClass*, pItem, ESI);

	AnimScales.erase(pItem);

	return 0;
}

// Step 4a PROBE -- read-only, changes nothing on screen. Co-tenant: Phobos
// AnimClass_DrawIt_DrawOffset at both addresses (always returns 0; ESI =
// anim, on-screen draw location at STACK_OFFSET(0x110, 0x4) = [esp+0x114]).
// Logs the first draws of scaled anims so an in-game run confirms (1) the
// CTOR handoff tags the right anims and (2) which DrawIt path they take,
// before 4b replaces the shape draw.
DEFINE_HOOK_AGAIN(0x422CD8, AnimClass_DrawIt_WarheadSizeProbe, 0x6)
DEFINE_HOOK(0x423122, AnimClass_DrawIt_WarheadSizeProbe, 0x6)
{
	GET(AnimClass* const, pThis, ESI);

	static int logged = 0;
	constexpr int MaxLines = 60;
	if (logged >= MaxLines || !pThis)
		return 0;

	auto const it = AnimScales.find(pThis);
	if (it == AnimScales.end())
		return 0;

	auto const pLocation = R->Stack<Point2D*>(0x114);
	++logged;
	Debug::Log("[WeaponExt][animscale] path=0x%X anim=%s scale=%.2f frame=%d screen=(%d,%d)%s\n",
		R->Origin(), pThis->Type ? pThis->Type->ID : "<null>", it->second,
		pThis->Animation.Value,
		pLocation ? pLocation->X : 0, pLocation ? pLocation->Y : 0,
		logged == MaxLines ? " [probe log limit reached]" : "");

	return 0;
}
