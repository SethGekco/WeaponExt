#pragma once

// Phase 0 diagnostics.
//
// Everything in DESIGN.md section 2 was derived by reading gamemd.exe with
// objdump. The probe turned those claims into measurements (section 14).
//
// This header now also carries the Phase 0.1 instrumentation for a second
// question: WHICH OF OUR INIT HOOKS ACTUALLY RUN.
//
// The banner never appeared in the first run. There were three candidate
// explanations and the log could not distinguish them:
//   (a) our ExeRun hook at 0x7CD810 is starved -- five frameworks hook that
//       address and Syringe's chain stops at the first non-zero return;
//   (b) it ran, but Debug::Log deferred and our finalize hook was also
//       starved;  [RULED OUT: Debug::Log does not defer, it JMP_STDs straight
//       into the game logger at 0x4068E0]
//   (c) it ran and logged, but 0x7CD810 is the first instruction of WinMain --
//       before the command line is parsed and the log file is opened -- so the
//       write went nowhere.
//
// (a) and (c) produce an identical empty log, so inferring between them is
// guessing. Instead each site sets a flag, and the flags are reported later
// from a site we have already proven works (the frame probe). One run, three
// answers.

namespace ScatterDiag
{
	// --- init-site markers, called from the hooks themselves ---
	void MarkExeRun();      // 0x7CD810, contested by 5 frameworks
	void MarkExeRunAlt();   // 0x7CD81E, hooked by nobody
	void MarkCmdLine();     // 0x52F639, contested by 6 frameworks

	// --- parse-path evidence ---
	// Proves the extension container hooks fire at all. The INI echo only
	// prints for weapons that configure something, so a mod with no ScatterExt
	// tags produces no evidence either way -- these counters do.
	void CountWeaponParse();
	void CountBulletParse();

	// Emitted once, from the first probe hit. Reports every flag above.
	// Deliberately NOT called at ExeRun: at that point the game has not parsed
	// its command line and the log file may not exist yet.
	void ReportOnce();

	// Should this shot be logged? The probe fires inside TechnoClass::Fire,
	// which runs for every shot from every unit, so it must be rate-limited --
	// an unbounded log would fill a disk in a skirmish.
	//
	// Three-tier budget, each tier added because the previous one failed in a
	// real game:
	//
	//  1. A single shared cap let E1 rifle fire eat 138 of 200 records before
	//     anything interesting happened.
	//  2. Splitting scattered from unscattered fixed that, but then a Flak
	//     Track took 192 of the 200 scattered slots and the Grand Cannon --
	//     the weapon actually under test -- got 8.
	//  3. So also cap PER WEAPON. No single high-rate-of-fire weapon can crowd
	//     out the one being investigated.
	//  4. `interesting` is now "ScatterExt acted on this shot OR the shot
	//     moved", not just "the shot moved". A direct-hit override and an
	//     in-accurate-zone shot both produce delta (0,0,0), so under the old
	//     rule they competed with rifle fire for the small unscattered budget
	//     -- which duly filled at 40/40 and made direct hits unobservable.
	bool ShouldLog(bool interesting, const char* weaponName);

	// Section 6 ROF-by-range. Separate small budget: the ROF hook fires on
	// every shot like the scatter probe, but a modder tuning a ROF curve wants
	// to see it independently of scatter records.
	void ROFLine(const char* weaponName, double distCells,
		int baseDelay, int finalDelay, double multiplier);

	// Emit one probe line. Kept out of the hook body so the hook stays a thin
	// register read and the formatting cost is easy to find in a profile.
	// `eng*` is what the engine produced, `fin*` what actually ships. They
	// differ exactly when ScatterExt took over, which makes the log a direct
	// before/after of the feature rather than just a record of the outcome.
	void ProbeLine(const char* firer, const char* target,
		int weaponIndex, const char* weaponName,
		int aimX, int aimY, int aimZ,
		int engX, int engY, int engZ,
		int finX, int finY, int finZ, bool overridden, bool directHit,
		double distanceLeptons);
}
