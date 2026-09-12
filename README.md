# WeaponExt

Standalone Syringe DLL for Yuri's Revenge, co-loaded with Antares (and
optionally Phobos). Five pillars — bounty, magnetron, mind control, power &
range logic, radiation fields. Full design in [DESIGN.md](DESIGN.md).

## Status

**P0** — read-only probe build. One observation hook at `0x702E64`
(`TechnoClass::RegisterDestruction`) logs every kill event: killer/victim
types and houses, alliance, victim Cost/Soylent, death cell. No behaviour is
modified.

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
