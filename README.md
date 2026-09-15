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

- Our probe co-hooks Antares' bounty address `0x702E64` at the same size;
  both handlers return 0 so the Syringe chain runs both. Our future bounty
  system enables via `Bounty.Hunter=`, deliberately NOT Antares' `Bounty=`.
- `0x702E6A` is left untouched: Phobos PR#2118 ("New bounty logic", open)
  owns it. Loading a Phobos build containing that PR alongside our bounty
  system will run two bounty systems; pick one.
- The DLL does nothing until added to the Syringe `-i=` list in wine-game.sh
  and to ClientDefinitions.ini.
