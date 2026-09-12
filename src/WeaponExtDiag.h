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
}
