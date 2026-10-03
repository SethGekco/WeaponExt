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

## Magnetron (M1) — quick reference

The Magnetron has no locomotor of its own: the stock warhead imbues the
**jumpjet** locomotor onto its victim. M1 reuses that rather than replacing
it, so the engine keeps doing the lift/carry/land *and* the release.

```ini
[SOMEMAGWARHEAD]              ; the IsLocomotor=yes warhead
; --- flight tuning. Applies ONLY when Locomotor= is the jumpjet CLSID
; {92612C46-F71F-11d1-AC9F-006008055BB5} (the stock magnetron value).
; Unset = keep whatever the victim's own TechnoType provides.
Magnetron.Speed=              ; integer
Magnetron.Climb=              ; float — ascent rate
Magnetron.Crash=              ; float — descent rate, i.e. how hard it lands
Magnetron.Height=             ; integer — carry altitude
Magnetron.Accel=              ; float
Magnetron.Wobbles=            ; float
Magnetron.Deviation=          ; integer
Magnetron.TurnRate=           ; integer

; --- letting go. Vanilla has NO beam-stop detection at all, so this is ours.
Magnetron.ReleaseOnStop=no    ; release when the beam stops hitting the victim
Magnetron.ReleaseOnStop.Delay=45 ; frames with no fresh beam hit before that
                              ; counts as stopped. MUST exceed the weapon's ROF
                              ; or the victim is dropped between shots.
Magnetron.MaxHoldTime=-1      ; frames; hard ceiling on a hold (-1 = none)
```

### Handoff — using any locomotor you like

The jumpjet is the only locomotor that knows how to hand control back. So
instead of avoiding the other CLSIDs, let your chosen locomotor do the
travelling and hand off to the jumpjet for the *ending*:

```ini
Magnetron.Handoff=yes             ; swap to the jumpjet when the job is done
Magnetron.Handoff.Lift=0          ; leptons. 0 (default) = INVISIBLE: no
                                  ; vertical movement, the jumpjet is used
                                  ; purely as the release mechanism
Magnetron.Handoff.Crash=          ; descent rate for the drop, if lifting
Magnetron.Handoff.OnStopped=yes   ; victim has stopped moving
Magnetron.Handoff.StoppedFor=15   ; ...for this many frames
Magnetron.Handoff.OnArrived=yes   ; victim reached the firer
Magnetron.Handoff.ArriveRange=2   ; ...within this many cells
Magnetron.Handoff.OnMindControl=yes ; victim got mind controlled
```

So `Locomotor={4A582741-…}` (Drive) + `Handoff=yes` gives you: the victim
drives to your tower under its own wheels, and the instant it stops it is
released cleanly, with no visible hop. `ReleaseOnStop`/`MaxHoldTime` stay
armed as backstops.

⚠ **About non-jumpjet `Locomotor=` values.** Setting `Locomotor=` to Drive,
Hover, Teleport, Tunnel, Walker, Missile or DropPod *imbues fine* but the
engine then has **no code anywhere** to release the victim — the
"give control back" logic lives inside `JumpjetLocomotionClass` alone. Those
victims are paralysed for the rest of the match in vanilla. `ReleaseOnStop`
and `MaxHoldTime` are the safety net and will free them, but the flight
overrides above do **not** apply, and the victim keeps the swapped locomotor
afterwards (which may move oddly if it does not suit the unit).

**Recommended:** either use `Magnetron.Handoff=yes` (above) so the engine's
release path still runs, or keep the jumpjet CLSID and shape the behaviour
with the tags — `Climb`/`Height`/`Speed` give arcs and lift heights, `Crash` gives
landing force. Background: encyclopedia `Magnetron-Locomotor-Imbue.md`.

Log markers: `[WeaponExt][mag N] released <victim> (held by <firer>) -- <why>`,
plus one `[WeaponExt] <WARHEAD>: magnetron overrides ...` line per configured
warhead at load (it states whether flight overrides will apply).
