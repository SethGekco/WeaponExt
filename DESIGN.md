# WeaponExt — Design Document

Standalone Syringe DLL for Yuri's Revenge, co-loaded with Antares (+ optionally
Phobos). Five pillars: **Bounty**, **Magnetron**, **Mind Control**, **Unusual
unit/building properties** (power & range logic), **Radiation**.

Status: design phase (2026-09-07; radiation pillar added 2026-09-11). No code yet.

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

## 6. Phase roadmap

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

Bounty first: fully understood funnel, zero RE risk, immediately testable.
R1 can run early too — it needs only our own containers plus the shared
warhead-detonate and logic-frame seats.

## 7. Standing-rule compliance
- Encyclopedia consulted (RegisterDestruction cluster, CaptureManager cluster,
  locomotor hooks); M0 findings go back in.
- Hook-overlap CI check wired in P0.
- New DLL must be added to the Syringe `-i=` list in wine-game.sh +
  ClientDefinitions.ini or it silently does nothing.
- Deploy after every green CI build (backup + byte-verify).
- No Phobos upstreaming; read-only reference. PR#2118 tag names deliberately
  not mirrored (our semantics differ; collision would be worse than contrast).
- Test with GI/GGI from Allied barracks, log owning house.
