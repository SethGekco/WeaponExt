# WeaponExt

Standalone Syringe DLL for Yuri's Revenge, co-loaded with Antares (and
optionally Phobos). Five pillars — bounty, magnetron, mind control, power &
range logic, radiation fields. Full design in [DESIGN.md](DESIGN.md).

## Status

- **P0 verified in-game** — the `0x702E64` kill probe logged live skirmish
  data (registers + victim liveness runtime-confirmed).
- **ScatterExt merged (2026-09-15)** — RangeScatter, per-axis scatter,
  ROF-by-range, curves/easing, InaccuracyModifier now live here; the
  ScatterExt repo is archived knowledge (its DESIGN.md + handoff stay
  authoritative for scatter internals). Log prefix is now `[WeaponExt]`.
  ScatterExt.dll must NOT be co-loaded with this DLL.

## Building

CI builds on push (MSBuild, `DevBuild|x86`). Submodules are pinned — clone
with `--recurse-submodules`.

## Compatibility notes

- We co-hook Antares' bounty address `0x702E64` at the same size; both
  handlers return 0 so the Syringe chain runs both. Our payout enables via
  `Bounty.Hunter=`, deliberately NOT Antares' `Bounty=`, so running both
  systems is an explicit opt-in rather than an accident. The victim-side
  `Bounty.Value=` key IS shared with Antares on purpose.
- `0x702E6A` is left untouched: Phobos PR#2118 ("New bounty logic", open)
  owns it. Loading a Phobos build containing that PR alongside our bounty
  system will run two bounty systems; pick one.
- The DLL does nothing until added to the Syringe `-i=` list in wine-game.sh
  and to ClientDefinitions.ini.

## Bounty (B1 + B2) — quick reference

Nothing pays out until some TechnoType opts in. Amounts come from the victim;
the earner decides whether it collects.

```ini
[General]                     ; mod-wide fallbacks (a type's own value wins)
Bounty.Value=0
Bounty.CostRatio=0.0          ; e.g. 1.0 => every kill is worth victim Cost=
Bounty.SoylentRatio=0.0

[SOMEVICTIM]
Bounty.Value=100              ; same key Antares reads, deliberately
Bounty.Value.Veteran=
Bounty.Value.Elite=
Bounty.CostRatio=
Bounty.SoylentRatio=          ; note: most buildings leave Soylent=0
Bounty.DeathReward=0          ; paid when THIS dies, regardless of killer
Bounty.DeathReward.Houses=owner

[SOMEHUNTER]
Bounty.Hunter=yes             ; the opt-in. NOT Antares' `Bounty=`
Bounty.Ratio=1.0
Bounty.Victims=               ; blank/all = everything. Mixed list:
                              ;   VehicleTypes,InfantryTypes,BuildingTypes,
                              ;   AircraftTypes, plus literal IDs (E1,MTNK)
                              ;   `none` disables. Unknown IDs are LOGGED.
Bounty.VictimHouses=enemies   ; owner|allies|enemies|team|all
Bounty.Receiver=killer        ; killer|victimHouse|killerAllies|none
Bounty.Payer=none             ; none|killer|victim  (payer loses the money)

[SOMELEECH]                   ; earns from kills it did not make
Bounty.Leech=yes
Bounty.Leech.Range=-1         ; cells; -1 = whole map
Bounty.Leech.Ratio=0.25
Bounty.Leech.Killers=         ; blank/all = any killer
Bounty.Leech.KillerHouses=all
Bounty.Leech.Victims=
Bounty.Leech.VictimHouses=enemies
```

Notes:
- **`enemies` includes the Neutral/civilian house** (anything not owner/ally).
- The killer never leeches its own kill; it already had the direct payout.
- Limboed objects (cargo inside a transport) do not leech.
- Log markers: `[WeaponExt][kill]` per kill, `[WeaponExt][pay N]` per money
  movement, plus one `[WeaponExt] <SECTION>: bounty ...` line at load per
  configured type.
