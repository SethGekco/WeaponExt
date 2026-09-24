# WeaponExt — Design Document

Standalone Syringe DLL for Yuri's Revenge, co-loaded with Antares (+ optionally
Phobos). Nine pillars: **Bounty**, **Magnetron**, **Mind Control**, **Unusual
unit/building properties** (power & range logic), **Radiation**,
**Scatter & fire control** (absorbed from ScatterExt), **Spawned missiles**,
**Limboed cargo** (garrison / passengers / bunker), **Warhead size scaling**.

Status: P0 verified in-game 2026-09-14 (bounty probe). ScatterExt merged
2026-09-15 — its DESIGN.md (22 sections) and HANDOFF-TO-WEAPONEXT.md in the
ScatterExt repo remain the source of truth for the scatter engine internals;
§6 here only covers what's NEW on top.

---

## 0. Prior art survey (what already exists — verified against local sources)

### Bounty
| Source | What it has | Where |
|---|---|---|
| Antares (release) | `Bounty=` (killer opt-in), `Bounty.Value=` (Promotable, on victim), `Bounty.Display=`, house `GivesBounty=`, rules `BountyEnablers=` (building list), `BountyDisplay=`. Kill-event only, no allies/self payout, fixed value only. Hook `0x702E64` (EDI=killer, ESI=victim). | `Antares-src/src/Ext/Techno/Hooks.Bounty.cpp`, `Body.cpp:1738` |
| Phobos **PR#2118 (open, unmerged)** | "New bounty logic": `Bounty.Enable`, `Bounty.Enablers`, `Bounty.Default`, cost-ratio via `Bounty.Multiplier`/`Bounty.KillerMultiplier` (per-rank variants), display tags. Hooks `0x702E6A` (0x7). Auto-disables Ares bounty. | encyclopedia `registry/pr-hooks.md` + GitHub |
| Kratos | No bounty system. | — |

**Not covered by anyone:** soylent-ratio payouts, global/local kill *leeching*
(earning from kills you didn't make), victim/killer type filters, arbitrary
house routing (pay the enemy, pay yourself on death), bounty multiplier
auras/warheads.

### Magnetron
- Vanilla magnetron: weapon warhead `IsLocomotor=yes` + `Locomotor=` CLSID
  piggybacks a locomotor onto the victim. Alternate CLSIDs mostly freeze the
  victim (see §2 notes from Rex's testing).
- No framework implements magnetron customization. Nearby known hooks:
  Phobos `0x4696CE` BulletClass_Detonate_ImbueLocomotor, `0x46954C`
  IsLocomotor bunker fix, PR#352 `0x469672`; droppod loco hooks Phobos
  `0x4B5B70`/`0x4B607D`, Antares/Ares `0x4B5EB0`/`0x4B5F9E`.
- Kratos has *mechanically adjacent* effects worth mining for approach (not
  code-copy): `BlackHoleState` (radius attraction, `Range/FullAirspace/Rate`),
  `PumpState` (toss unit in an arc, `Lobber=` high/flat arc — lift+landing
  physics done via simulated ballistics, no locomotor swap), `FreezeState`,
  `TeleportState`. Path: `KratosPP_meep/src/Ext/StateType/State/`.

### Mind control
- Phobos (release): `MindControlRangeLimit=`, `MindControl.IgnoreSize=`,
  `MindControlLink.VisibleToHouse=`, `MultiMindControl.ReleaseVictim=`.
  Range limit enforced per-frame in `TechnoExt::ApplyMindControlRangeLimit`
  (releases victim when `DistanceFrom > limit`).
- Antares/Ares: permanent MC, MC decisions in CaptureManager hooks.
- CaptureManager funnel (encyclopedia-verified): `0x471C90/0x471C96`
  CanCapture, `0x471D2E` Is_Controllable (Kratos), `0x471D40` CaptureUnit,
  `0x471FF0` FreeUnit, `0x4721E6/0x472198` link drawing, `0x4722AA`
  ControlledCount, `0x47243F` DecideUnitFate, `0x4726C7` IsOverloading.
  MC application from bullets: `0x4692BD` BulletClass_Logics_ApplyMindControl.
- Nobody has: timed temp→perma, timed perma→release, house routing of the
  captured unit, MC *link/relay* units, resistance/charge-time, variable slot
  weight, per-slot power drain.

### Power / range
- Phobos shields = the reference implementation for "second bar" rendering +
  `Powered=` interaction semantics.
- No framework has local power pools, weapon power costs, or
  cellspread-augmented range. Range-check precedent: Phobos PR#2088
  `0x4D5A34` FootClass_ApproachTarget_StopWhenInRange.

### Radiation
- Phobos (release) owns this subsystem almost completely: **RadTypes** (named
  radiation types per warhead: `SiteWarhead(.Detonate/.Full)`, `Color`,
  `DurationMultiple`, `ApplicationDelay(.Building)`, `BuildingDamageMaxCount`,
  `LevelMax/LevelDelay/LevelFactor`, `LightDelay/LightFactor/TintFactor`,
  `HasOwner`, `HasInvoker`) + **16 registry hooks** across `0x65B28D–0x65BE01`
  (CTOR/DTOR, Activate cluster, AI delays, UpdateLevel ×3, Deactivate) plus
  the damage-application sites `0x4DA59F` FootClass_AI_Radiation, `0x43FB23`
  BuildingClass_AI_Radiation, `0x469150` BulletClass_Detonate_ApplyRadiation.
  Source: `Phobos/src/New/Type/RadTypeClass.*`, `Ext/RadSite/*`.
- Antares/Ares touch only `0x65B5FB` (Radiate snow-color unhardcode).
- Structural facts: a rad site = `BaseCell` + `Spread` (cells) + one scalar
  level; per-cell level = radial falloff from center, quantized to cells
  (hence the "pixelated circle" look); fade = uniform level decay, so the
  visible shrink is *emergent* (outer, weaker cells hit zero first). Cell tint
  is per-cell palette math — the engine cannot draw sub-cell radiation.
- Nobody has: noise/messy footprints, sub-cell fields, smooth-drawn circles,
  conditional spreading, contamination trails, signed shrink/fade rates, or
  condition-gated rad properties.

### Limboed cargo
- Vanilla: `BuildingClass::KillOccupants(TechnoClass*)` @ `0x4585C0`
  (`BuildingClass.h:193`), driven by `Assaulter=yes`. All-or-nothing,
  garrison-only, warhead has no say. **No passenger equivalent exists.**
- Antares/Ares hook `0x458729` *inside* that function (raid status, ESI =
  building) — co-tenancy to respect, but not our seat.
- Phobos: `PassengerDeletion.*` (transport eats its own cargo, with
  `.DontScore`), `DriverKilled.KillPassengers`, `OpenTransport.RangeBonus/
  DamageMultiplier` — adjacent, none warhead-driven. Its warhead effects
  **explicitly skip limbo**: `Ext/WarheadType/Body.cpp:114` guards on
  `!IsOnMap || !IsAlive || InLimbo || IsSinking`.
- Nobody has: warhead-driven damage to cargo of any kind, ejection-based
  correct death animations, or per-container filters.

---

## 1. Pillar: Bounty

One primitive: a **payout rule** evaluated at the kill-registration funnel.
Every kill produces an event `(killer, victim, victimCost, victimSoylent)`;
rules decide who pays, who receives, and how much.

### 1.1 Payout amount (victim-side tags)
```ini
[VICTIM]
Bounty.Value=100          ; Antares-compatible base (flat)
Bounty.Value.Veteran=     ; per-rank, like Antares Promotable
Bounty.Value.Elite=
Bounty.CostRatio=0.0      ; + ratio of victim Cost=
Bounty.SoylentRatio=0.0   ; + ratio of victim Soylent=
```
Amount = flat + cost·ratio + soylent·ratio (components sum; all default off).

### 1.2 Killer-side enable + filters
```ini
[HUNTER]
Bounty.Hunter=yes             ; our opt-in (deliberately NOT Antares' Bounty= to avoid double-pay)
Bounty.Ratio=1.0              ; killer-side scale
Bounty.Victims=VehicleTypes,E1,MTNK,YACNST,BuildingTypes
                              ; mixed list: class-list names expand, IDs literal; empty = all
Bounty.VictimHouses=enemies   ; owner|allies|team|enemies|neutral|all combos
```

### 1.3 Kill leeching (global/local tracking)
A unit/building earns from kills **it didn't make**:
```ini
[LEECH]
Bounty.Leech=yes
Bounty.Leech.Range=-1         ; cells; -1 = whole map (global == local with -1, per Rex)
Bounty.Leech.Ratio=0.25
Bounty.Leech.Killers=         ; which killer types feed this leech (empty = all)
Bounty.Leech.KillerHouses=owner,allies
Bounty.Leech.Victims=         ; victim filter, same mixed-list parsing
Bounty.Leech.VictimHouses=enemies
```
Implementation: our TechnoExt keeps a registry vector of live leech units; the
kill event iterates it with the range/filters. Range measured from the leech
unit to the victim's death coordinates.

### 1.4 House routing (who pays, who gets)
```ini
[ANY]
Bounty.Receiver=killer        ; killer|victimHouse|allies-of-killer|specific country list
Bounty.Payer=none             ; none (money from thin air)|victim|killer — payer LOSES the money
Bounty.DeathReward=0          ; victim-side: paid when this unit dies
Bounty.DeathReward.Houses=owner ; who receives the death reward
```
This covers all requested cases: enemy kills my unit and I get paid
(`Bounty.DeathReward`), I kill an enemy and *pay them*
(`Bounty.Receiver=victimHouse` + `Bounty.Payer=killer`), only specific houses,
everyone, etc. House lists accept country names and the affected-house enum.

### 1.5 Bounty multipliers (auras + warheads)
Never generate money; only scale payouts computed above.
```ini
[TECHNO]
BountyBonus.Hunter=1.0        ; multiplies payouts EARNED by affected units
BountyBonus.Victim=1.0        ; multiplies payouts GIVEN when affected units die
BountyBonus.Range=5           ; aura radius; -1 = global
BountyBonus.Houses=owner,allies

[WARHEAD]
BountyBonus.Hunter=2.0        ; timed, applied to everything in CellSpread
BountyBonus.Victim=0.5
BountyBonus.Duration=300      ; frames
```
Stacking rule: multipliers multiply together; document it.

### 1.6 Hook plan
- Primary funnel: co-hook `0x702E64` (size 0x6, same as Antares — same-address
  hooks chain; Antares returns 0 so the chain continues to us). Antares only
  pays killers with its `Bounty=yes`, so keeping our enable tag separate makes
  double-pay impossible unless the modder opts into both.
- **Do NOT hook `0x702E6A`** — reserved by Phobos PR#2118 (adjacent to
  0x702E64+6, no overlap, but if that PR merges and is loaded, both systems
  would run; note in README).
- Wire the hook-overlap CI check (standing rule) from day one.

---

## 2. Pillar: Magnetron

The most RE-heavy pillar. Rex's verified behavior notes for `IsLocomotor=yes`
warheads with alternate `Locomotor=` CLSIDs:

| Locomotor | Observed |
|---|---|
| Missile `{B7B49766-E576-11d3-9BD9-00104B972FE8}` | nothing; unit freezes |
| Teleport `{4A582747-…}` | teleports victim to firer, then disabled (held-in-sky state) |
| Aircraft `{4A582746-…}` | nothing; momentary freeze |
| Drive `{4A582741-…}` | victim drives to firer, then disabled |
| Hover `{4A582742-…}` | nothing; freeze |
| Tunnel `{4A582743-…}` | nothing; freeze |
| DropPod `{4A582745-…}` | victim falls from sky onto firer's position, instantly destroyed |
| Walker `{55D141B8-DB94-11d1-AC98-006008055BB5}` | nothing; freeze |

Root-cause hypothesis (to verify in M0): the piggyback path never *releases*
the victim (grabbed state never cleared) and non-magnetron locomotors never get
a destination/completion signal, so the victim stops taking orders permanently.
"Fixed" means: piggyback ends cleanly when the beam stops, victim resumes
normal command response.

### 2.1 Features
```ini
[MAGWEAPON→Warhead]
Magnetron.ReleaseOnStop=yes     ; un-stick victims when beam stops (the core fix)
Magnetron.VictimCanFire=no      ; allow firing while grabbed
Magnetron.Speed=                ; pull speed override
Magnetron.LandingDamage=0       ; damage (or % of victim health) on touchdown
Magnetron.LandingWarhead=       ; optional warhead detonated at touchdown

; Destination control (default = vanilla: cell adjacent to firer)
Magnetron.Dest=firer            ; firer|victim|coords
Magnetron.Dest.Offset=0,0       ; cells, meaning depends on frame:
Magnetron.Dest.Frame=firerFacing ; firerFacing|firer|victim|map
                                ; firerFacing → "away from me" = negative X
Magnetron.Dest.Types=           ; e.g. YAPSYT,MIND → send victim toward nearest listed type
Magnetron.Dest.Types.NeedFreeSlots=no ; only count mind controllers with open MC slots
Magnetron.LiftOnly=no           ; lift straight up, hold, drop in place (stall weapon)
Magnetron.LiftOnly.Height=      ; leptons
Magnetron.LiftOnly.Duration=    ; frames held

; Arc / parabola control
Magnetron.Arc=straight          ; straight|ballistic|missile
Magnetron.Arc.Height=           ; apex height (ballistic)
Magnetron.Gravity=              ; parabola shape override
```
DropPod mode becomes customizable: `Magnetron.DropPod.Teleport=no` (travel
instead of blink), `.Speed=`, `.SurviveImpact=yes`, `.ImpactDamage=`.

Missile-locomotor mode = `Magnetron.Arc=missile`: fly the victim in an arc to
the destination instead of vanilla up-across-down.

### 2.2 Implementation strategy
Two candidate mechanisms — decide in M1 after RE:
1. **Fix the real locomotor path**: hook MagnetronLocomotionClass process +
   the imbue/release sites (`0x4696CE` neighborhood). Honest but each alternate
   locomotor is its own bug surface.
2. **Kratos-style simulated ballistics** (their `PumpState`): don't swap
   locomotors at all; take control of the victim's coordinates each frame,
   restore cleanly. One code path handles arcs, lift-only, landing damage, and
   release for free. Strong precedent that this works online-safely.

Recommendation: mechanism 2 for everything except a genuine locomotor swap;
keep the vanilla magnetron CLSID working via mechanism 1's release fix.

### 2.3 RE checklist (Phase M0)
- [ ] MagnetronLocomotionClass vtable + CLSID (PDB map names only the
      WaveClass_Draw sites `0x7601C7/0x7601FB/0x760286`; loco internals
      unnamed — disassemble)
- [ ] Where the victim's "grabbed" state lives + why it never clears
- [ ] Beam-stop detection point (weapon stops firing / firer dies / retargets)
- [ ] Fire-veto location for `VictimCanFire` (per-class GetFireError funnels)
- Consult the encyclopedia first (standing rule); contribute findings back.

---

## 3. Pillar: Mind Control

Extends the CaptureManager. All state on our TechnoExt; per-frame countdowns in
our update hook. Must coexist with Phobos `MindControlRangeLimit` and
Antares/Ares permanent MC.

```ini
[CONTROLLER]
MindControl.PermaDelay=-1        ; frames after capture → becomes permanent (-1 off)
MindControl.Duration=-1          ; frames after capture → auto-release (-1 off)
MindControl.House=owner          ; who inherits the victim (owner|specific country|neutral)
MindControl.ChargeTime=0         ; frames of sustained targeting before capture fires
MindControl.IgnoreImmunity=no    ; pierce ImmuneToPsionics=yes
MindControl.OnlyImmune=no        ; ONLY target ImmuneToPsionics=yes (requested)

[VICTIM]
MindControl.Resistance=1.0       ; scales ChargeTime (ratio) …
MindControl.Resistance.Flat=0    ; … or adds flat frames; applies to perma conversion too
MindControl.SlotsTaken=1         ; how many controller slots this victim occupies

[RELAY]                          ; link units: no weapon, no slots of their own
MindControl.Link=yes
MindControl.Link.Range=10        ; extends the effective control/range-limit web
MindControl.Link.Master=         ; which controller types it relays for (empty = any owned)

[CONTROLLER or WARHEAD]          ; range-limit modifiers (Phobos-interop)
MindControlRangeLimit.Modifier=1.0    ; aura or warhead-applied, ratio
MindControlRangeLimit.Modifier.Range=   ; aura radius
MindControlRangeLimit.Modifier.Houses=enemies ; e.g. enemy jammer shrinks your MC reach
MindControlRangeLimit.Modifier.Duration=      ; warhead: timed

[CONTROLLER-BUILDING]
MindControl.PowerPerSlot=0       ; extra power drain per occupied slot (can be negative)
```

Notes:
- "Perma with countdown-release" = don't use Ares perma at all; our
  `MindControl.Duration` on an otherwise-permanent controller gives the same
  result cleanly.
- Slot weight (`SlotsTaken`) hooks the ControlledCount/IsOverloading sites
  (`0x4722AA`, `0x4726C7`) — count weights, not nodes.
- Link relays: range-limit becomes "victim within X of the controller OR any
  live relay in the web"; must run *before* Phobos's per-frame release check
  would fire (verify ordering — RE item).
- Magnetron `Dest.Types.NeedFreeSlots` reuses the same CaptureManager
  accessors.

---

## 4. Pillar: Unusual unit/building properties

### 4.1 Local power
```ini
[TECHNO]
Power.Local=yes            ; internal power pool, invisible to the house grid
Power.Amount=100           ; pool size
Power.Recharge=0           ; per-frame regen (0 = fixed pool, never refills)
Power.Bar=no               ; render as a second bar (shield-style pips)
```
### 4.2 Weapons that consume power
```ini
[WEAPON]
Weapon.PowerCost=20        ; drawn from local pool if Power.Local, else house surplus
Weapon.RequiresPower=no    ; hard veto when unpayable / house is low-power
```
Veto point: GetFireError funnels per class (encyclopedia precedents:
Infantry `0x51CB8C/0x5206E4`, Building `0x447FAE` Ares PrismForward).

### 4.3 CellSpread-augmented range
```ini
[TECHNO or WEAPON]
Weapon.RangeUsesCellSpread=no  ; range 12 + CellSpread 2 → engage at 14, aim short
```
Fire at the *cell* just inside true range so splash covers the target.
Touch points: in-range checks + targeting evaluation (RE: the
`TechnoClass::InRange`-family call sites; precedent PR#2088 `0x4D5A34`).
Homing-projectile caveat: needs target-cell substitution, not just a range lie.

---

## 5. Pillar: Radiation

### 5.0 Architecture decision — parallel field system, NOT RadSiteClass hooks

Phobos fully occupies the `0x65B` RadSiteClass window (16 hooks) and replaces
chunks of the level math. Layering our features there means fighting their
handlers on every address. Instead: **RadFields** — our own site objects in
our own container, spawned by our own warhead tag, with our own per-frame
damage pass and our own renderer. Vanilla/Phobos radiation stays untouched and
fully usable alongside. This also happens to be the only way to get sub-cell
fields and signed fade rates at all, since `RadSiteClass` structurally cannot
express them (one scalar level + cell-quantized falloff).

```ini
[SOMEWARHEAD]
RadField.Type=GreenGoo        ; spawns a RadField instead of / alongside vanilla rad

[RadFieldTypes]
0=GreenGoo

[GreenGoo]                    ; defaults mirror Phobos RadType names where sensible
RadField.Warhead=RadSite      ; damage warhead
RadField.Level=500            ; center level
RadField.Radius=5.0           ; cells, fractional allowed (sub-cell: <1.0 works)
RadField.ApplicationDelay=16
RadField.Duration=900         ; frames before fade begins (-1 = until level empties)
RadField.Color=0,255,0
```

### 5.1 Messy / pixelated / splatter footprints
Per-cell level = radial falloff × noise. Noise must be **deterministic**
(affects damage → sim state): hash of (site creation frame, base cell, cell) —
per the synced-vs-hashed rule this is save/replay/MP-safe with no RNG stream
consumption.
```ini
RadField.Shape=circle         ; circle|noisy|splatter|ring
RadField.Noise=0.0            ; 0..1 amplitude of per-cell level jitter
RadField.Noise.Holes=0.0      ; 0..1 chance a cell is skipped entirely (the "messy" look)
RadField.Noise.Scale=1        ; cells per noise sample (1 = per-cell pixel mess,
                              ; larger = blobby patches)
```
`splatter` = a few random sub-blobs around the impact instead of one disc.

### 5.2 Sub-cell fields & smooth rendering (Rex's two questions — both feasible)
- **Perfect circle within a cell (gameplay):** YES. Our damage pass measures
  *lepton* distance from field center to each object's exact coordinates; a
  `Radius=0.4` field damages only objects physically inside that circle, even
  mid-cell. Vanilla can't do this because damage keys off the victim's cell.
- **Drawing perfect circles instead of the pixelated cell tint:** YES, with a
  custom draw layer (same family as WaveClass/EBolt/laser drawing — precedent:
  the magnetron WaveClass_Draw sites `0x7601C7/0x7601FB/0x760286`). We render
  a translucent filled circle/ellipse in tactical view and set no cell tint at
  all. Render-side jitter (shimmer, edge flicker) uses **hashed** randomness,
  never the synced RNG.
```ini
RadField.Draw=cells           ; cells (classic tint)|smooth (drawn circle)|both|none
RadField.Draw.Alpha=40        ; smooth-mode translucency
RadField.Draw.EdgeSoftness=0.2
```
RE item: pick the draw hook (tactical overlay pass) — consult encyclopedia,
contribute back. Cell-tint mode reuses per-cell palette tint the way vanilla
does, but driven from our field function so noise/holes show up in it too.

### 5.3 Conditional spreading + contamination trails
Spreading = cellular pass over our field's cells every `Rate` frames:
```ini
RadField.Spread=no
RadField.Spread.Rate=90            ; frames between spread steps
RadField.Spread.Threshold=100      ; min cell level to seed a neighbour
RadField.Spread.Ratio=0.5          ; seeded level = source × ratio
RadField.Spread.MaxRadius=12       ; hard cap; -1 = unlimited (map-eater, allowed)
RadField.Spread.Terrain=           ; optional whitelist (Clear,Road,Water…)
```
Trails — a unit driving through hot cells becomes a carrier and lays a trail:
```ini
RadField.Trail=no
RadField.Trail.PickupThreshold=200 ; cell level needed to contaminate a passer-by
RadField.Trail.Duration=300        ; frames the carrier keeps dripping
RadField.Trail.Level=100           ; level laid per visited cell
RadField.Trail.Spread=1.0          ; radius (cells) laid around the carrier's path
RadField.Trail.Falloff=0.9         ; per-cell-laid decay along the trail
```
Carrier state lives on our TechnoExt; movement sampling per frame from our
update hook (cheap: compare last cell). Kratos `TrailType` is the mechanical
reference for trail-laying cadence. Carriers can seed new spreadable fields —
document the combination (`Trail` + `Spread`) as intentionally cascading.

### 5.4 Signed shrink & fade rates
Vanilla's shrink-while-vanishing is emergent; we make both axes explicit and
**signed** (negatives allowed for completion, as requested):
```ini
RadField.Fade.LevelRate=10         ; level lost per LevelDelay tick;
                                   ;   negative = field grows HOTTER (capped at Level)
RadField.Fade.RadiusRate=0.0       ; cells lost per tick; negative = radius GROWS
                                   ;   (a second, smoother way to spread)
RadField.Fade.Mode=edge            ; edge (classic: rim dies first)|uniform|center
                                   ;   center = ring-out death, for completeness
```
Growth caps: `LevelMax`, `Spread.MaxRadius` reused. A field with both
negatives and no caps is a modder foot-gun — warn in docs, don't forbid.

### 5.5 Condition-gated properties (prerequisite system)
Reuse the PrerequisiteExt **Requirement** primitive shape (types + house scope
+ polarity) rather than inventing a new grammar. A RadFieldType lists override
profiles; each profile has a condition evaluated against the **field's owner
house** (we always track owner + invoker, like Phobos `HasOwner/HasInvoker`):
```ini
[GreenGoo]
RadField.Profiles=Enriched,Suppressed

[Enriched]                          ; applies while condition holds
Requirement.RequiredBuildings=NAPULS
Requirement.Houses=owner
RadField.Level=800                  ; any RadField.* tag may be overridden
RadField.Fade.LevelRate=-5

[Suppressed]
Requirement.ForbiddenBuildings=GAWEAT
Requirement.Houses=enemies          ; e.g. enemy counter-structure weakens it
RadField.Level=200
```
Evaluation: at spawn, and re-checked every `RadField.Profiles.Rate=` frames
(0 = spawn-time only). First matching profile wins; document the ordering.
Victim-side gates also supported: `RadField.Immune.Houses=`,
`RadField.Immune.Types=` on the field type.

### 5.6 Hook plan (deliberately tiny)
- Spawn: read `RadField.Type=` in our existing warhead-detonation touchpoint
  (shared with §1.5 warhead multipliers — one detonate hook serves both;
  co-exists with Phobos `0x469150` since we never touch vanilla rad spawning).
- Per-frame: our existing logic-frame seat (encyclopedia: `0x55B6B3`
  uncontended post-loop) drives field AI, spread, trails, profiles.
- Damage: our own pass calls the standard damage-dealing API with the field's
  warhead — no hooks into Foot/Building AI radiation sites needed.
- Draw: ONE render hook for smooth mode (M0-style RE task, encyclopedia
  first). Cell-tint mode needs a cell-tint touchpoint — check what Phobos'
  TintFactor sites do and pick a non-conflicting seat (RE item).

---

## 6. Pillar: Scatter & fire control (ScatterExt merged + failure tracking)

### 6.0 What came over in the merge (2026-09-15)
RangeScatter curves, per-axis ellipsoid scatter, ROF-by-range, plot-point
curves + easing, InaccuracyModifier — plus the engine knowledge (Fire frame
map, both vanilla scatter formulas, the 0x6FE8EE seat, the 0x6FF29E rearm
store) and the native curve test suite (979 checks, runs in CI).
**Protected invariant: a weapon with no scatter tags consumes ZERO random
numbers.** Log prefixes are now `[WeaponExt]`; markers `[WEAPONEXT]` /
`[WEAPONEXT direct-hit]` / `[engine]`. Canaries 0x5CA77E00/01/02.
Still pending from ScatterExt: in-game observation of ROF-by-range,
InaccuracyModifier, plot curves (staged rulesmd config exists); §20
distribution, §7/§8 EvaluateObject gates (relative-branch trap!) designed
only.

### 6.1 Weapon failure tracking (Rex's cannon-miss system)
The scatter engine gives us something no other framework has: **we know the
scattered offset at fire time** (we computed it). A shot whose offset exceeds
the warhead's lethal radius against the aimed target is a predicted miss the
frame it fires. Detection therefore has two tiers:
- **Predicted**: offset > lethal radius at 0x6FE8EE → count immediately.
- **Confirmed** (covers moving targets + arcing travel time): at bullet
  detonation, distance(impact, intended target) > failure radius → count.
Tier choice per weapon: `Failure.Detect=predicted|confirmed|either` (default
confirmed). Counts are consecutive per (firer, weapon slot, target); any
qualifying hit resets.

```ini
[TECHNOTYPE]                       ; all tags exist for Primary./Secondary./
                                   ; WeaponX./ElitePrimary./EliteSecondary./
                                   ; EliteWeaponX. prefixes
Primary.FailureLimit=5             ; 5 consecutive misses vs one target →
                                   ; trigger the block below once (re-arms:
                                   ; every further 5 misses fires it again)
Primary.FailureBehavior=Blacklist  ; Blacklist | Reposition | Switch | Stalk | Correct
Primary.Failure.AttachEffect=      ; see 6.1.2
Primary.FailureWeapon=120mm        ; temporary weapon override on trigger
Primary.FailureWeapon.Shots=5      ; revert after N shots …
Primary.FailureWeapon.Time=100     ; … or after N frames (whichever first;
                                   ; -1 disables that axis)
Primary.FailureWeapon.Scatter.Min=0        ; absolute override …
Primary.FailureWeapon.Scatter.Max=1
Primary.FailureWeapon.Scatter.MinAdjust=-2 ; … or additive adjust …
Primary.FailureWeapon.Scatter.MaxAdjust=2
Primary.FailureWeapon.Scatter.MinMult=0.5  ; … or multiplier (checked in this
Primary.FailureWeapon.Scatter.MaxMult=2    ; order: absolute > adjust > mult)
```

Behaviors:
- **Blacklist** — never auto-target that object again until it changes cell.
  (Implemented in target evaluation; ⚠ the EvaluateObject threat gates steal
  relative branches — never `return 0` there, see handoff §6.)
- **Reposition** — scatter-move one cell, then re-engage.
- **Switch** — drop target, re-evaluate; old target eligible again after a
  cooldown (`Failure.Switch.Cooldown=`, default ~300 frames).
- **Stalk** — hold fire on that target until it moves. For static targets
  (buildings) Stalk degrades to Switch automatically.
- **Correct** — aim at a random cell adjacent to the target so splash damage
  connects (reuses the §4.3 aim-at-cell machinery).

#### 6.1.1 Scope + storage
Per-instance state on our TechnoExt: {targetPtr, weaponSlot, consecutive
misses, active override + shots/time remaining, blacklist set (target ptr +
cell-at-blacklist)}. Bullet→(firer, slot, intended target) link stored on our
BulletExt at fire time (we already own the fire seat). Invalidation: the
engine-call-invalidates-your-guard rule applies — re-validate target pointers
every read; blacklist entries die with the target.

#### 6.1.2 Interop (all optional, all fail-soft)
- **`Failure.AttachEffect`** — we do NOT reach into Phobos's AE internals
  (co-loaded-ext trap, AbstractClass+0x18 lesson). Instead the tag names a
  **warhead** we detonate on the firer at trigger time; that warhead carries
  the Phobos `AttachEffect.*` tags (or anything else). Works with zero Phobos
  coupling; if Phobos absent, the warhead simply does whatever it does.
- **TraitExt** — our tags are plain INI keys, so TraitExt's INI-level
  inheritance/random/modifier machinery applies to them for free. No runtime
  linkage; nothing to detect. If deeper hooks are ever wanted, detect via
  `GetModuleHandleA("TraitExt.dll")` and no-op when absent.
- **TechnoAttachmentExt** — Reposition/Stalk must not order attached
  (map-mode container) units around. Guard: skip movement behaviors for
  objects whose locomotor CLSID is TAExt's private one; degrade to Switch.
  No TAExt loaded → the check never matches → no cost.

### 6.2 Scatter modifiers as timed effects (the "AE support" ask)
Same pattern as BountyBonus/RadField profiles: warhead-applied **timed
InaccuracyModifier** on our own ext (`InaccuracyModifier.Attach=`,
`.Duration=`, `.Houses=`) rather than reading Phobos AE state. Multiplies
with all other sources (invariant: sources multiply, never add).

---

## 7. Pillar: Spawned missiles (V3/DMISL family)

Prior art: Ares/Antares **CustomMissile** (per-weapon custom missile types;
hooks 0x6622E0 + takeoff cluster), Kratos **KamikazeTracker** ext
(0x54E478–0x54E56D — proof that spawned missiles can chase a live target),
Phobos spawner customizations (`Spawner.LimitRange/DelayFrames/
AttackImmediately/RecycleRange…`) and cruise-missile hooks. RocketLocomotion
`Process` is a **crowded window** (Antares CustomMissile owns several sites) —
every hook here gets checked against the registry first.

### 7.1 DynamicLocking (chase the moving target)
```ini
[SPAWNER-TECHNOTYPE]
Missile.DynamicLocking=no          ; missile re-aims at the target's LIVE
                                   ; position each frame instead of the cell
                                   ; it was ordered at
Missile.DynamicLocking.IntervalLimit=150 ; extra flight budget in frames:
   ; at launch we record the planned ETA to the ORIGINAL cell; chasing may
   ; extend flight time; once (actual elapsed − planned ETA) exceeds this,
   ; the timeout behavior fires
Missile.DynamicLocking.TimeoutBehavior=detonate ; detonate (self-destruct
   ; airborne) | dive (straight down onto current position, normal warhead)
   ; | weapon (detonate Missile.DynamicLocking.TimeoutWeapon= instead)
Missile.DynamicLocking.TimeoutWeapon=
```
Feasibility: yes — the rocket locomotor flies a precomputed profile toward a
stored destination; updating the destination each frame is exactly what
Kratos's kamikaze mode does. The planned-ETA bookkeeping lives on our ext for
the missile unit, computed at launch from the vanilla profile
(distance/speed), so the budget check is one subtraction per frame.

### 7.2 Missile inaccuracy (on the spawning TechnoType)
Vanilla spawned missiles fly exactly to the ordered cell. New: the missile
itself draws its miss at launch — tags live on the **spawner TechnoType**,
the draw happens when the missile launches, using the merged scatter engine
(synced RNG, zero-draw invariant applies: no tags → no draw):
```ini
Missile.Scatter.Max=2.0            ; cells; full per-axis/curve grammar from
Missile.Scatter.Min=0.0            ; §6 available under this prefix
```
Scattered *destination*, not scattered flight — works with DynamicLocking
(the chase offset re-applies the drawn miss around the live position).

### 7.3 Per-rank / conditional custom missiles
```ini
Missile.Type=DMISL                 ; base (Ares CustomMissile remains usable)
Missile.Type.Veteran=              ; rank overrides
Missile.Type.Elite=
Missile.Profiles=Blessed           ; Requirement-gated overrides, same
                                   ; profile grammar as RadField §5.5
```
Selection happens at spawn creation (SpawnManager site), so each launched
missile evaluates rank/conditions at that moment.

### 7.4 Building "without ordnance" turret voxels (V3WO fix)
Vehicles get `<Image>WO.vxl` swapping via Ares `NoSpawnAlt=yes`; buildings'
turret draw path never consults spawn state. Fix: hook the building turret
draw to select the WO turret voxel while `SpawnManager` has the missile out.
`Building.NoSpawnAltTurret=yes` + optional `TurretWO=` explicit image name.
RE item: building turret draw site + where vehicle NoSpawnAlt does the swap
(read Ares source for the pattern, reimplement).

### 7.5 Anti-air missile spawns
Vanilla spawned missiles cannot engage air. Two gates to open: the spawner
weapon's targeting check (projectile `AA=` equivalent for the spawn weapon)
and the missile's destination logic (an air target's position is 3-D and
moving — which is DynamicLocking with Z tracking). Ships as
`Missile.CanTargetAir=yes`, requires `Missile.DynamicLocking=yes` (documented
dependency; a fixed-cell missile vs a mover would always whiff). Timeout
rules from 7.1 apply — an outrun missile dives or detonates.

---

## 8. Pillar: Limboed cargo (garrison / passengers / bunker)

One primitive: **units the engine is holding in limbo**, which warheads
currently cannot reach. `ObjectClass::InLimbo` is documented in YRpp as "act as
if it doesn't exist" — that is exactly why damage, AttachEffects and position
effects all miss them ([[yr-garrisoned-infantry-are-limboed]]).

### 8.0 The three containers (all verified in YRpp, no Phobos coupling)
| Container | Type | Where |
|---|---|---|
| Garrison occupants | `DynamicVectorClass<InfantryClass*> Occupants` | `BuildingClass.h:314` |
| Transport cargo | `PassengersClass { int NumPassengers; FootClass* FirstPassenger; }` | `TechnoClass.h:124`; walk via `ObjectClass::NextObject` (`ObjectClass.h:291` — "next object in the same cell **or transport**") |
| Tank/battle bunker | `BuildingClass::BunkerLinkedItem` (single link) | already mapped during BunkerExt |

⚠ **Open-topped passengers are the same container but a different state.** Per
the encyclopedia (`Ext-Building-Occupancy.md`), open-topped cargo is registered
into the **logic layer** and fires its own weapons — so it is partially present
while still limboed. Treat it as its own filter case (`Passengers.OpenTopped=`)
rather than assuming all passengers are equally invisible.

### 8.1 What vanilla gives us (and why this is new ground)
`BuildingClass::KillOccupants(TechnoClass* pAssaulter)` → **`0x4585C0`**
(`BuildingClass.h:193`), driven by `Assaulter=yes`. It is all-or-nothing,
garrison-only, and the warhead has no say. There is no passenger equivalent.
Adjacent prior art that is *not* this feature: Phobos `PassengerDeletion.*`
(the transport eats its own cargo) and `DriverKilled.KillPassengers` — neither
is warhead-driven.

Co-tenancy: **Antares and Ares both hook `0x458729`**, *inside* `KillOccupants`
(`BuildingClass_KillOccupiers_AllOccupantsKilled`, ESI = building, for raid
status). We do **not** hook this function at all — our trigger is the warhead
detonation seat we already own (shared with §1.5 bounty multipliers and §5.6
RadField spawning: one seat, three features).

### 8.2 Ejection is the design, not an option
Rather than faking a death animation, **unlimbo the contained unit onto a free
cell, then detonate the real warhead on it.** Correct per-warhead death comes
free — burned, gibbed, vaporized, electrocuted — along with veterancy, score,
EVA and crate logic. Faking it would get each of those subtly wrong, and
differently wrong per warhead.

This also solves the buff-effect problem cleanly: Phobos skips limboed targets
outright (`Ext/WarheadType/Body.cpp:114` — `!IsOnMap || !IsAlive || InLimbo ||
IsSinking`), so an ejected unit is momentarily a **normal on-map target** and
Phobos's AttachEffect sees it with no interop code on our side. (Remember
`CellSpread!=0` is required for Phobos warhead effects to apply at all —
[[phobos-warhead-effects-need-cellspread]].)

```ini
[WARHEAD]
; Prefixes: Occupants. (garrison) · Passengers. (cargo) · Bunker. (tank bunker)
; `Contained.` sets all three at once; a specific prefix overrides it.
Occupants.Damage=200          ; 0/unset = this warhead ignores cargo entirely
Occupants.Warhead=            ; warhead to use on them (default: this warhead)
Occupants.Eject=damage        ; no      = damage in place, generic death
                              ; damage  = eject, detonate, survivors re-enter
                              ; always  = eject, detonate, survivors stay out
Occupants.Eject.Fallback=inplace ; inplace | skip  (when no free cell exists)
Occupants.Max=-1              ; cap affected per detonation (-1 = all)
Occupants.Houses=all          ; whose cargo is eligible
Occupants.Types=              ; optional type filter (mixed list, as §1.2)
Passengers.OpenTopped=yes     ; include open-topped (logic-layer) cargo
```

### 8.3 The four hazards, designed for up front
1. **Unlimbo can fail** — a bunkered building hemmed in by walls has no free
   cell. Cell search is bounded; on failure `Eject.Fallback` decides between
   killing in place (no per-warhead anim) or skipping. Never leave a unit
   half-ejected.
2. **Never mutate a container while iterating it.** Snapshot the pointers into
   a fixed-capacity local array first, iterate the copy, and **re-validate
   liveness before every single step** — `Unlimbo` and damage both re-enter the
   engine and can invalidate pointers mid-hook
   ([[engine-call-invalidates-your-guard]]).
3. **Cell choice must be deterministic** or it desyncs. Fixed scan order —
   foundation cells first, then expanding rings in a fixed compass order,
   bounded radius. **No RNG at all**, not even the synced stream, so the cost
   is zero and the sync argument is trivial.
4. **Double-kill guard.** If the same detonation destroys the container, vanilla
   teardown kills the remaining cargo itself (`ClearBunker()` is literally
   documented as "content is dead — chronosphered away or died inside"). Snapshot
   before container damage, and track a per-detonation flag so cargo is never
   killed twice.

### 8.4 The trap the container structure hides
Garrison firing uses a **single shared occupy-weapon slot arbitrated by
`FiringOccupantIndex`** (encyclopedia, `Ext-Building-Occupancy.md`). Removing an
occupant from the middle of `Occupants` can therefore leave that index dangling
or pointing at the wrong infantryman — a state vanilla only ever reaches through
its own removal paths. **RE item, do before writing the removal code:**
disassemble `0x4585C0` and mirror exactly what vanilla does to `Occupants` +
`FiringOccupantIndex` when it kills occupants; do not invent our own removal.

Other RE items: `Unlimbo` placement semantics for infantry vs vehicles; whether
bunker teardown needs `ClearBunker` called explicitly after we empty the link.

---

## 9. Pillar: Warhead size scaling

Multipliers that grow (or shrink) a warhead's `CellSpread`, sourced from the
firing house's **country** and from **timed effects**, with per-warhead
exemptions and clamps — plus the matching question of drawing explosions
bigger.

### 9.0 The core problem
`CellSpread` lives on the shared `WarheadTypeClass`; every house detonates
the same object. There is no per-house field to set. Options considered:
1. **Patch the register inside `MapClass::DamageArea`** where the spread is
   loaded. Cleanest for vanilla damage, but Phobos reads `CellSpread` itself
   for its warhead effects (AE, shields, …) and would see the unscaled radius.
2. **Scale-and-restore around `BulletClass::Detonate`** — write the scaled
   value on entry, put it back on exit. Everything inside the detonation sees
   it, co-loaded extensions included. **Chosen.**

Sync: the scaled value is a pure function of INI data + the firing house, so
every client computes the same number. No RNG is consumed.

### 9.1 Tags
```ini
[Americans]                      ; country section (read by WeaponExt directly —
WarheadSize.Multiplier=1.25      ;   no linkage to CountryExt.dll needed)

[CombatDamage]                   ; universal limits — all unset by default (no limit)
; Which warheads get scaled at all (compared against the warhead's OWN CellSpread):
WarheadSize.IgnoreSpreadBelow=1.0  ; CellSpread under 1.0 → never scaled
WarheadSize.IgnoreSpreadAbove=     ; CellSpread over this → never scaled
; How far a warhead can be scaled:
WarheadSize.MultiplierCap=2.0    ; the combined multiplier never goes above ×2.0
WarheadSize.MultiplierFloor=0.5  ; …or below ×0.5 (only matters for shrinking)
WarheadSize.SpreadCap=8          ; enlarging stops at 8 cells; a warhead that is
                                 ;   already bigger than 8 is left as it is
WarheadSize.SpreadFloor=0.5      ; shrinking stops at 0.5 cells; one already
                                 ;   smaller is left as it is

[SOMEWARHEAD]
WarheadSize.Exempt=no            ; yes = never scaled (nukes, SW, rad sites…)
; Any of the six [CombatDamage] keys above can be set here too, and wins over
; the universal value for this warhead only.
WarheadSize.FromZero=0           ; CellSpread=0 × anything is 0. When > 0, a
                                 ;   CellSpread=0 warhead is treated as this
                                 ;   spread before filtering and scaling.
                                 ;   (Phobos warhead effects need CellSpread≠0 —
                                 ;   this is what lets scaling switch them on.)

; step 2 — timed effect, same pattern as BountyBonus / InaccuracyModifier.Attach
[BUFFWARHEAD]
WarheadSize.Attach=1.5
WarheadSize.Attach.Duration=300
WarheadSize.Attach.Houses=owner,allies
```
Evaluation order, per detonation:
1. Combine all sources (they multiply — standing invariant), then apply
   `MultiplierCap` / `MultiplierFloor`. Negative → 0. Exactly 1.0 → stop.
2. Base = the warhead's CellSpread, or `FromZero` if CellSpread is 0 (no
   `FromZero` → stop).
3. Base outside `IgnoreSpreadBelow`…`IgnoreSpreadAbove` → stop.
4. `scaled = base × multiplier`, then `SpreadCap` (when enlarging) or
   `SpreadFloor` (when shrinking). Limits only stop a change partway; they
   never reverse it.

#### Per-warhead overrides — worked example
All six `[CombatDamage]` keys can also go on a warhead. A key set on a
warhead replaces the universal value **for that warhead only**; keys it does
not set still come from `[CombatDamage]`. Using the `[CombatDamage]` values
above (`IgnoreSpreadBelow=1.0`, `SpreadCap=8`, `SpreadFloor=0.5`):
```ini
[FlakWH]                         ; CellSpread=0.5
WarheadSize.IgnoreSpreadBelow=0  ; opt back IN: the universal filter skips
                                 ;   anything under 1.0, so without this line
                                 ;   FlakWH would never be enlarged. 0 = no
                                 ;   lower filter for this warhead.
                                 ;   SpreadCap/Floor still come from
                                 ;   [CombatDamage] (8 / 0.5).

[ShellWH]                        ; CellSpread=2
WarheadSize.SpreadCap=3          ; tighter limit: this warhead stops growing at
                                 ;   3 cells, even though others may reach 8
WarheadSize.SpreadFloor=1.5      ; and stops shrinking at 1.5 cells
                                 ;   (universal floor is 0.5)

[BigBombWH]                      ; CellSpread=6
WarheadSize.MultiplierCap=1.2    ; this one only ever gets ×1.2 at most,
                                 ;   while others can reach the universal ×2.0

[NukeWH]
WarheadSize.Exempt=yes           ; never scaled at all, whatever the limits say
```
With an American ×1.25 multiplier, those come out as: FlakWH 0.5 → 0.625;
ShellWH 2 → 2.5 (under its cap of 3); BigBombWH 6 → 7.2 (×1.2, not ×1.25);
NukeWH unchanged.

`IgnoreSpreadBelow` / `IgnoreSpreadAbove` are a **filter**: they decide
*whether* a warhead is scaled at all, by comparing its own (unscaled)
CellSpread. The `Cap` / `Floor` keys are **limits**: they decide *how far*
an eligible warhead can be scaled.

Per-warhead value > `[CombatDamage]` value > no limit. A map's
`[CombatDamage]` overrides the rules one (read through Phobos's
`0x679A15` LoadBeforeTypeData seat, once per INI).
**Zero-cost invariant:** multiplier exactly 1.0 → no frame, no write.

Attaching via Phobos AttachEffect: per §6.1.2 we don't read Phobos AE state;
a Phobos AE can instead detonate `BUFFWARHEAD` to apply ours.

### 9.2 Hook seats (step 1)
Both co-tenanted with Phobos (`src/Ext/Bullet/Hooks.DetonateLogics.cpp`),
both `return 0` on Phobos's normal path; sizes match Phobos's declarations:

| Addr | Size | Phobos co-tenant | Use |
|---|---|---|---|
| `0x4690C1` | 8 | `BulletClass_Logics_DetonateOnAllMapObjects` (ESI=bullet) | apply |
| `0x469AA4` | 5 | `BulletClass_Logics_Extras` (ESI=bullet) | restore |

Detonate has an EBP frame (Phobos reads coords at `[ebp+0x8]`).
Warhead container: Phobos's `0x75D1A9` CTOR (EBP), `0x75E5C8` SDDTOR (ESI),
`0x75DEA0` LoadFromINI (ESI, INI at `[esp+0x150]`). Country tag: Phobos's
`0x51214F`/`0x51215A` HouseType LoadFromINI (EBX, INI at `[ebp+0x8]`).

### 9.3 Nesting / early-exit discipline
- A detonation kills something whose death weapon detonates **inside** the
  outer DamageArea → frames form a LIFO stack.
- Same warhead nested → the inner frame scales from the **true original**
  (oldest live frame for that warhead), never from the outer scaled value.
- Early exits that skip the restore seat (e.g. Phobos returning
  `ReturnFromFunction` at `0x4690C1` after our handler ran) → each frame
  records its Detonate's EBP. On any later apply, frames with EBP ≤ current
  EBP have provably returned (a live caller sits higher on the stack) and are
  unwound. On restore, deeper frames (EBP < current) unwind first.

### 9.4 Beyond the engine's spread cap (step 3)
Vanilla area damage iterates a fixed cell-offset table (roughly 10–11 cells,
**to verify**); `WarheadSize.Max=15` cannot reach past it on its own. The
extra ring needs our own damage pass (the RadField §5.6 machinery):
`WarheadSize.Overflow=yes` damages objects between the table cap and the
scaled radius, with vanilla `PercentAtMax` falloff extended over the full
radius.

### 9.5 Bigger explosion drawing (steps 2 and 4)
- **Step 2 — art swap (zero RE):** `WarheadSize.AnimList.Scaled=` +
  `WarheadSize.AnimList.Thresholds=` pick larger pre-drawn anims when the
  effective multiplier crosses a threshold. Co-seat: Phobos
  `0x469C46` `BulletClass_Logics_DamageAnimSelected` (EBX = anim type) —
  that handler returns `SkipGameCode`, so we cannot co-hook it blindly; RE
  item: pick a seat after it or override via its own tags.
- **Step 4 — true scaling:** voxel anims scale via their draw matrix (easy).
  SHP anims: the shape blitter is 1:1 only, so hook the anim draw, render the
  frame to a scratch surface, stretch-blit (nearest-neighbour, same
  palette/ConvertClass + translucency). Hazards: the anim's dirty-rect must
  grow or large frames clip/trail; shadow frames need the same treatment; CPU
  cost with many anims. **Render-only → no sync risk.** No known framework
  does SHP scaling.

### 9.6 Known gaps in step 1 (to close)
- Multiplier is read at **detonation** from `Bullet->Owner->Owner`. A firer
  that dies before impact → unscaled. Fix in step 2: capture onto a BulletExt
  at fire time (the same link §6.1.1 needs).
- Phobos registers no save/load hooks for WarheadTypeExt and neither do we
  (INI-only data). Verify the tags survive save → load in-game.
- Chain order at `0x469AA4` vs Phobos's Extras handler is load-order
  dependent: if ours runs first, Phobos's warhead effects see the unscaled
  radius. Vanilla damage is always scaled. RE item: a restore seat after
  Extras (Detonate epilogue) would make Phobos effects scale deterministically.
- Addresses not yet checked against the Hook Encyclopedia in this session —
  the CI overlap/bounds check does that on the first PR build.

---

## 10. Phase roadmap

| Phase | Deliverable |
|---|---|
| P0 | Repo scaffold (pin submodules to TechnoAttachmentExt commits!), CI + hook-overlap check, probe hook at 0x702E64 logging kill events |
| B1 | Bounty core: value/cost/soylent ratios, hunter enable, victim/house filters |
| B2 | Leeching (global/local), house routing, DeathReward |
| B3 | Multiplier auras + warhead multipliers |
| M0 | Magnetron RE (checklist §2.3) + encyclopedia contribution |
| M1 | Release fix + VictimCanFire + speed/landing damage (pick mechanism) |
| M2 | Destination control + arcs + LiftOnly + DropPod customization + Dest.Types |
| C1 | MC timers (PermaDelay/Duration), House routing, immunity tags |
| C2 | ChargeTime/Resistance, SlotsTaken, PowerPerSlot |
| C3 | Links/relays + range-limit modifiers (Phobos interop) |
| U1 | Local power + weapon power costs |
| U2 | Power bar rendering |
| U3 | CellSpread range |
| R1 | RadField core: spawn tag, cell-tint draw, damage pass, fade rates (signed), noise/splatter shapes |
| R2 | Spreading + contamination trails; sub-cell radius damage |
| R3 | Smooth-circle renderer + condition profiles (Requirement reuse) |
| S1 | ~~ScatterExt merge~~ **DONE 2026-09-15** (CI green, deployed, ScatterExt de-listed) |
| S2 | Observe the pending ScatterExt features in-game (staged rulesmd config); §20 distribution |
| F1 | Failure tracking: detection tiers, counters, Blacklist/Switch/Stalk/Correct |
| F2 | FailureWeapon override + scatter adjusts; Failure.AttachEffect warhead; Reposition |
| SM1 | Missile.Scatter (destination draw at launch) |
| SM2 | DynamicLocking + IntervalLimit + TimeoutBehavior; CanTargetAir |
| SM3 | Per-rank/profile missiles; building WO turret voxel fix |
| L0 | RE `0x4585C0` KillOccupants: mirror vanilla's `Occupants` + `FiringOccupantIndex` bookkeeping (§8.4); probe logging container contents at detonation |
| L1 | Passengers + Occupants damage with `Eject=no` (in-place), snapshot/liveness discipline |
| L2 | Ejection path: deterministic cell search, `Eject=damage/always`, fallbacks, double-kill guard |
| L3 | Bunker link, open-topped filter, house/type filters, `Contained.` shorthand |
| W1 | **Started.** Country `WarheadSize.Multiplier`; `[CombatDamage]` + per-warhead `IgnoreSpreadBelow/Above`, `MultiplierCap/Floor`, `SpreadCap/Floor`; `Exempt`, `FromZero`; Detonate scale-and-restore (§9.2–9.3) |
| W2 | `WarheadSize.Attach` timed effect; fire-time capture on BulletExt; AnimList threshold swap |
| W3 | Overflow damage pass beyond the engine spread cap |
| W4 | RE anim draw seat; true SHP/voxel draw scaling |

Bounty first: fully understood funnel, zero RE risk, immediately testable.
R1 can run early too — it needs only our own containers plus the shared
warhead-detonate and logic-frame seats. L-phases share that same detonate seat
but need L0's RE before any removal code is written.

## 11. Standing-rule compliance
- Encyclopedia consulted (RegisterDestruction cluster, CaptureManager cluster,
  locomotor hooks); M0 findings go back in.
- Hook-overlap CI check wired in P0.
- New DLL must be added to the Syringe `-i=` list in wine-game.sh +
  ClientDefinitions.ini or it silently does nothing.
- Deploy after every green CI build (backup + byte-verify).
- No Phobos upstreaming; read-only reference. PR#2118 tag names deliberately
  not mirrored (our semantics differ; collision would be worse than contrast).
- Test with GI/GGI from Allied barracks, log owning house.
