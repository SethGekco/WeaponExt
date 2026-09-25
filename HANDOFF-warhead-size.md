# Handoff: Warhead size scaling (WeaponExt PR #1)

Briefing for a new (local) session picking this work up. Written 2026-09-25 by
the cloud session that built steps 1–4a. Everything referenced here is pushed.

## Where things are

| What | Where |
|---|---|
| PR | `SethGekco/WeaponExt#1` (draft), branch `claude/warhead-multipliers-animation-scaling-cnk0ib` → `master` |
| Design | `DESIGN.md` §9 (tags §9.1, hooks §9.2, nesting §9.3, engine limit §9.4, anims §9.5, **known gaps §9.6**), roadmap rows W1–W4 in §10 |
| Code | `src/Hooks.WarheadSize.cpp` (all logic), `src/Ext/WarheadType/Body.{h,cpp}` (per-warhead tags) |
| In-game test | `tests/ingame/warheadsize-test.ini` (rulesmd append + what to look for in debug.log) |
| Encyclopedia | `SethGekco/YR-Hook-Encyclopedia` `main`: `encyclopedia/Bullet-Detonate-DamageArea.md`, `encyclopedia/Anim-Lifecycle-DrawIt.md`, 16 WeaponExt rows in `registry/hooks.csv` |
| Build | CI on the PR (hook overlap/bounds vs encyclopedia registry, curve tests, MSBuild DevBuild). Latest head is green, and the DLL is the run's `WeaponExt` artifact. CI builds only `master`/`main` pushes and PRs. |

## What's built (all CI-green, **none tested in-game**)

- **W1:** country `WarheadSize.Multiplier`; `[CombatDamage]` + per-warhead
  `IgnoreSpreadBelow/Above`, `MultiplierCap/Floor`, `SpreadCap/Floor`;
  `Exempt`, `FromZero`. `CellSpread` is scaled on entry to
  `BulletClass::Detonate` (`0x4690C1`) and restored at `0x469AA4`, using an
  EBP-keyed frame stack for nesting and early exits.
- **W2:** `WarheadSize.Attach/.Duration/.Houses` timed buff (applied at
  `0x469AA4`); fire-time capture of multiplier + house at `0x6FF660`
  (bullet at `[esp+0x3C]`); `WarheadSize.AnimList.Scaled/.Threshold` swaps the
  EBX anim pick at `0x469C46`.
- **W3:** engine spread-table length (`0x7ED3D0`) measured at runtime and
  logged; clamp to max(limit, the warhead's own CellSpread);
  `WarheadSize.Overflow` ring pass via `ReceiveDamage` at edge distance;
  `[CombatDamage] WarheadSize.EngineSpreadLimit=` overrides the measurement.
- **W4a:** `WarheadSize.AnimScale/.Max` tags the created anim (one-shot
  handoff consumed by `AnimClass_CTOR` `0x4226F6`); a **read-only** probe on
  DrawIt `0x423122`/`0x422CD8` logs `[WeaponExt][animscale] …`. Nothing is
  drawn bigger yet.

All hook addresses, sizes and registers were copied from Phobos source
(`develop`, 2026-09-25), not guessed. Every seat co-tenants an existing
Phobos hook at the same size.

## Do next, in order

1. **Deploy + in-game test.** Grab the latest CI artifact DLL. Add
   WeaponExt.dll to the Syringe `-i=` list in `wine-game.sh` and
   `ClientDefinitions.ini`, **before Phobos**. Apply
   `tests/ingame/warheadsize-test.ini` to rulesmd.ini, replacing `TESTWH` with
   a real, visible splash warhead. `[CBEProbeWH]` / `CBEProbeEffect` already
   exist in rulesmd; CommandBarExt tactical-bar **button 14** detonates
   CBEProbeWH on the selected own units through a real `BulletClass::Detonate`,
   which becomes the one-click ×2 buff. Follow the steps in the test file.
2. **Read debug.log** and record:
   - `WarheadSize: engine spread limit measured as N cells` + the raw-entries
     line → **add N to the encyclopedia** (`Bullet-Detonate-DamageArea.md`,
     "Measured value", and tick its RE-VERIFY item).
   - Whether `[animscale]` lines appear, and which `path=`. If B (scaling)
     works but these are **absent**, Phobos's always-non-zero handler at
     `0x469C46` starves ours. Record that in the encyclopedia: it settles the
     open chain question in `Syringe-Stub-Semantics.md`, which is currently
     unresolved.
   - Outer-ring victims: **edge damage** (expected) or **full damage**. If
     full, the falloff lives in DamageArea and `ApplyOverflow` must scale the
     damage itself (see §9.6).
   - Visual: the buffed unit's splash is wider for ~60 s, then back to normal.
3. **Step 4b (the actual bigger drawing)** needs disassembly of `gamemd.exe`
   around `0x4232E0`–`0x423370`. Find the main SHP draw `CALL` between
   `0x4232EA` and `0x423365`: its address, argument layout and stolen bytes.
   The checklist is in DESIGN.md §9.5 and in the encyclopedia's
   `Anim-Lifecycle-DrawIt.md`. Don't write the stretched blitter until that
   call is mapped.
4. Update the PR description's test-plan checkboxes as items pass. The PR
   stays a draft until in-game testing is done.

## Gotchas

- **Load order matters** for the anim swap/scale (`0x469C46`): WeaponExt must
  come before Phobos, because Phobos's handler there always returns
  `SkipGameCode`.
- **Still not persisted:** step 2 state (Attach effects, per-shot captures) is
  lost on save → load. This is documented, not a bug to chase now.
- **Button 14 doesn't exercise fire-time capture** (no `FireAt`). To test that,
  kill the buffed unit while its shell is in the air; it should still land
  scaled.
- **Owner preference:** always consult the YR-Hook-Encyclopedia before hooking
  and add findings back to it. The encyclopedia has a neutrality rule: pages
  are about vanilla + public frameworks, and private DLLs only get incidental
  mentions. Registry rows for private DLLs are fine. Changes go straight to
  `main`.
- **Standing rules** from DESIGN.md §11: test with GI/GGI from the Allied
  barracks and log the owning house; deploy after green CI (backup +
  byte-verify); no Phobos upstreaming.
