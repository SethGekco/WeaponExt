#pragma once

// P0 diagnostics for the bounty probe.
//
// Init-site liveness is instrumented, not assumed (ScatterExt lesson: a
// starved hook and a too-early log write are indistinguishable in the log).
// The markers are reported from the first probe hit -- a site proven live by
// the very fact it is reporting.

namespace WeaponDiag
{
	// --- init-site markers, called from the hooks themselves ---
	void MarkExeRun();      // 0x7CD810, contested by 5 frameworks
	void MarkExeRunAlt();   // 0x7CD81E, hooked by nobody

	// Emitted once, from the first probe hit.
	void ReportOnce();

	// Kill events are far rarer than shots, but a long comp-stomp still
	// produces thousands; cap the log so a soak test can't flood debug.log.
	bool ShouldLog();

	// One line per actual money movement. Separately budgeted from the kill
	// log: a single kill can pay many houses (allies, death rewards, and one
	// line per leeching object), so sharing the kill budget would let one
	// busy frame hide every later payout. Lesson from ScatterExt's four
	// budget iterations -- the states you care about must stay distinguishable.
	void PayoutLine(const char* reason, const char* earner,
		const char* paidHouse, int amount);

	// Magnetron releases. Separately budgeted again: these are rare but a
	// stuck-victim bug shows up as a FLOOD, so the cap has to be its own.
	void MagnetronLine(const char* why, const char* victim, const char* firer);
}
