// Native tests for the curve engine (DESIGN.md section 4).
//
// Curve.h depends only on <cmath>, so it compiles and runs on the build host
// with no game, no YRpp and no Windows. That matters: everything else in this
// project can only be verified by launching Yuri's Revenge and reading a log,
// which is slow and costs a human a play session. The curve maths is the one
// part that does not have to be tested that way, so it shouldn't be.
//
// This closes a real gap. Through Phase 1 only System=Parabola and
// Increment.Mode=Peak were ever exercised in-game; Line, Exponential, Cubic
// and Mode=Rate were implemented but had never been run at all.
//
//   g++ -std=c++20 -I../src curve_test.cpp -o curve_test && ./curve_test

#include <Curve.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	int Failures = 0;
	int Checks = 0;

	void Check(bool ok, const std::string& what)
	{
		++Checks;
		if (!ok)
		{
			++Failures;
			std::printf("  FAIL: %s\n", what.c_str());
		}
	}

	void Near(double got, double want, const std::string& what, double eps = 1e-9)
	{
		++Checks;
		if (std::fabs(got - want) > eps)
		{
			++Failures;
			std::printf("  FAIL: %s (got %.12f, want %.12f)\n", what.c_str(), got, want);
		}
	}

	const char* Name(ScatterSystem s)
	{
		switch (s)
		{
		case ScatterSystem::Line:        return "Line";
		case ScatterSystem::Exponential: return "Exponential";
		case ScatterSystem::Parabola:    return "Parabola";
		case ScatterSystem::Cubic:       return "Cubic";
		}
		return "?";
	}
}

int main()
{
	using S = ScatterSystem;
	using M = IncrementMode;

	// ---- Endpoints ---------------------------------------------------------
	// Every monotonic system must span exactly [0,1] so that Increment means
	// "the scatter at the curve's peak" and nothing else.
	std::printf("endpoints\n");
	for (auto s : { S::Line, S::Exponential, S::Cubic })
	{
		Near(Curve::Shape(s, 0.0), 0.0, std::string(Name(s)) + " shape(0) == 0");
		Near(Curve::Shape(s, 1.0), 1.0, std::string(Name(s)) + " shape(1) == 1");
	}

	// Parabola is the deliberate odd one out: a VALLEY. Worst at both ends of
	// the region, perfect in the middle. Confirmed as intentional by Rex --
	// it models a weapon with a sweet spot, so do not "fix" it to monotonic.
	std::printf("parabola valley\n");
	Near(Curve::Shape(S::Parabola, 0.0), 1.0, "Parabola shape(0) == 1 (worst)");
	Near(Curve::Shape(S::Parabola, 0.5), 0.0, "Parabola shape(0.5) == 0 (trough)");
	Near(Curve::Shape(S::Parabola, 1.0), 1.0, "Parabola shape(1) == 1 (worst)");
	Check(Curve::Shape(S::Parabola, 0.25) > Curve::Shape(S::Parabola, 0.4),
		"Parabola descends into the trough");
	Check(Curve::Shape(S::Parabola, 0.75) > Curve::Shape(S::Parabola, 0.6),
		"Parabola climbs back out");
	// Symmetric about the trough.
	for (double d = 0.05; d <= 0.5; d += 0.05)
		Near(Curve::Shape(S::Parabola, 0.5 - d), Curve::Shape(S::Parabola, 0.5 + d),
			"Parabola symmetric about t=0.5", 1e-12);

	// ---- Monotonicity and curvature ---------------------------------------
	std::printf("monotonicity + curvature\n");
	for (auto s : { S::Line, S::Exponential, S::Cubic })
	{
		double prev = -1.0;
		for (double t = 0.0; t <= 1.0001; t += 0.01)
		{
			const double v = Curve::Shape(s, std::fmin(t, 1.0));
			Check(v >= prev - 1e-12, std::string(Name(s)) + " is monotonic non-decreasing");
			Check(v >= -1e-12 && v <= 1.0 + 1e-12, std::string(Name(s)) + " stays within [0,1]");
			prev = v;
		}
	}
	Near(Curve::Shape(S::Line, 0.5), 0.5, "Line is exactly linear at the midpoint");
	// Exponential and Cubic both start slower than Line and finish steeper --
	// that is the whole point of offering them.
	Check(Curve::Shape(S::Exponential, 0.5) < 0.5, "Exponential lags Line at midpoint");
	Check(Curve::Shape(S::Cubic, 0.5) < 0.5, "Cubic lags Line at midpoint");
	Near(Curve::Shape(S::Cubic, 0.5), 0.125, "Cubic midpoint is t^3");

	// ---- Progress clamping -------------------------------------------------
	std::printf("progress clamping\n");
	Near(Curve::Progress(15.0, 20.0, 30.0), 0.0, "below region clamps to 0");
	Near(Curve::Progress(35.0, 20.0, 30.0), 1.0, "above region clamps to 1");
	Near(Curve::Progress(25.0, 20.0, 30.0), 0.5, "midpoint of region");
	Near(Curve::Progress(25.0, 30.0, 30.0), 0.0, "degenerate region yields 0");

	// ---- Accurate zone -----------------------------------------------------
	// Must be EXACTLY zero, not merely small: a weapon inside its comfortable
	// band has to behave precisely as vanilla. Verified in-game too (section 16:
	// all 16 shots inside 20 cells scattered exactly 0).
	std::printf("accurate zone\n");
	for (auto s : { S::Line, S::Exponential, S::Parabola, S::Cubic })
		for (double d : { 0.0, 5.0, 19.0, 19.999 })
			Near(Curve::Evaluate(s, M::Peak, 2.5, d, 20.0, 30.0), 0.0,
				std::string(Name(s)) + " gives exactly 0 below Region.min");

	// ---- Peak mode is bounded by Increment --------------------------------
	std::printf("Peak mode bounds\n");
	for (auto s : { S::Line, S::Exponential, S::Parabola, S::Cubic })
	{
		// Sweep for the "never exceeds" half...
		double worst = 0.0;
		for (double d = 20.0; d <= 30.0; d += 0.05)
			worst = std::fmax(worst, Curve::Evaluate(s, M::Peak, 2.5, d, 20.0, 30.0));
		Check(worst <= 2.5 + 1e-9, std::string(Name(s)) + " Peak never exceeds Increment");

		// ...but test the endpoint EXPLICITLY for the "actually reaches" half.
		// `d += 0.05` accumulates float error and stops at 29.95, so the sweep
		// alone reports a peak of 2.4875 and looks like a bug in the curve when
		// it is only a gap in the sampling. (It did, on first run.)
		worst = std::fmax(worst, Curve::Evaluate(s, M::Peak, 2.5, 30.0, 20.0, 30.0));
		Near(worst, 2.5, std::string(Name(s)) + " Peak actually reaches Increment", 1e-6);
	}

	// The worked example from DESIGN.md section 4, which is also the config
	// that was verified in-game to 1 lepton of error (section 16).
	std::printf("worked example (Region 20-30, Increment 2.5, Parabola)\n");
	Near(Curve::Evaluate(S::Parabola, M::Peak, 2.5, 20.0, 20.0, 30.0), 2.5, "20c -> 2.5 cells");
	Near(Curve::Evaluate(S::Parabola, M::Peak, 2.5, 25.0, 20.0, 30.0), 0.0, "25c -> 0 cells");
	Near(Curve::Evaluate(S::Parabola, M::Peak, 2.5, 30.0, 20.0, 30.0), 2.5, "30c -> 2.5 cells");
	// The three distances actually measured in-game, in leptons.
	Near(Curve::Evaluate(S::Parabola, M::Peak, 2.5, 27.61, 20.0, 30.0) * 256.0,
		174.0, "27.61c -> ~174 leptons (matches the in-game measurement)", 1.0);
	Near(Curve::Evaluate(S::Parabola, M::Peak, 2.5, 29.01, 20.0, 30.0) * 256.0,
		412.0, "29.01c -> ~412 leptons (matches the in-game measurement)", 1.0);

	// ---- Rate mode is unbounded -------------------------------------------
	std::printf("Rate mode\n");
	Near(Curve::Evaluate(S::Line, M::Rate, 0.5, 30.0, 20.0, 30.0), 5.0,
		"Line Rate: 0.5 per cell over 10 cells == 5");
	Check(Curve::Evaluate(S::Line, M::Rate, 0.5, 60.0, 20.0, 30.0) > 2.5,
		"Rate exceeds Increment beyond the region -- it is deliberately uncapped");
	Near(Curve::Evaluate(S::Cubic, M::Rate, 1.0, 22.0, 20.0, 30.0), 8.0,
		"Cubic Rate is (d-min)^3");
	Near(Curve::Evaluate(S::Exponential, M::Rate, 1.0, 23.0, 20.0, 30.0), 9.0,
		"Exponential Rate is (d-min)^2");

	// Parabola has no sensible unbounded form -- its valley is defined by being
	// normalised to the region -- so Rate must silently fall back to Peak.
	// The parser warns about this; the engine must not misbehave regardless.
	std::printf("Parabola + Rate falls back to Peak\n");
	for (double d = 20.0; d <= 30.0; d += 0.5)
		Near(Curve::Evaluate(S::Parabola, M::Rate, 2.5, d, 20.0, 30.0),
			Curve::Evaluate(S::Parabola, M::Peak, 2.5, d, 20.0, 30.0),
			"Parabola Rate == Parabola Peak");

	// ---- Determinism -------------------------------------------------------
	// These values feed fire/hold decisions that must agree across clients, so
	// the same inputs must always give bit-identical output. No RNG in here.
	std::printf("determinism\n");
	for (auto s : { S::Line, S::Exponential, S::Parabola, S::Cubic })
	{
		const double a = Curve::Evaluate(s, M::Peak, 2.5, 27.3456, 20.0, 30.0);
		const double b = Curve::Evaluate(s, M::Peak, 2.5, 27.3456, 20.0, 30.0);
		Check(a == b, std::string(Name(s)) + " is bit-identical on repeat");
	}

	// ---- PlotCurve: user-drawn piecewise-linear curves (section 21) --------
	std::printf("PlotCurve basics\n");
	{
		PlotCurve c;
		Check(!c.IsSet(), "empty curve is unset");
		Near(c.Evaluate(5.0), 0.0, "empty curve evaluates to 0");

		c.Add(10.0, 2.0);
		Check(c.IsSet(), "one point makes it set");
		Near(c.Evaluate(0.0), 2.0, "single point is constant below");
		Near(c.Evaluate(10.0), 2.0, "single point is constant at");
		Near(c.Evaluate(99.0), 2.0, "single point is constant above");
	}

	std::printf("PlotCurve interpolation + clamping\n");
	{
		PlotCurve c;
		c.Add(10.0, 0.0);
		c.Add(20.0, 4.0);
		Near(c.Evaluate(10.0), 0.0, "at first point");
		Near(c.Evaluate(20.0), 4.0, "at last point");
		Near(c.Evaluate(15.0), 2.0, "linear midpoint");
		Near(c.Evaluate(12.5), 1.0, "linear quarter");
		// Clamped, NOT extrapolated: a typo must not send the curve to infinity.
		Near(c.Evaluate(0.0), 0.0, "clamps below the first point");
		Near(c.Evaluate(1000.0), 4.0, "clamps above the last point");
	}

	std::printf("PlotCurve ordering is by range, not insertion\n");
	{
		// INI keys arrive in whatever order they were typed. At[30] written
		// above At[20] must still describe the same curve.
		PlotCurve a, b;
		a.Add(10.0, 1.0); a.Add(20.0, 3.0); a.Add(30.0, 2.0);
		b.Add(30.0, 2.0); b.Add(10.0, 1.0); b.Add(20.0, 3.0);
		for (double r = 5.0; r <= 35.0; r += 0.5)
			Near(a.Evaluate(r), b.Evaluate(r), "insertion order is irrelevant");
		Check(a.Count == 3 && b.Count == 3, "both hold 3 points");
	}

	std::printf("PlotCurve duplicates + capacity\n");
	{
		PlotCurve c;
		c.Add(10.0, 1.0);
		c.Add(10.0, 5.0);
		Check(c.Count == 1, "duplicate range does not add a point");
		Near(c.Evaluate(10.0), 5.0, "duplicate range: last one wins");

		PlotCurve full;
		for (int i = 0; i < MaxCurvePoints; ++i)
			Check(full.Add(i * 1.0, i * 1.0), "fits within capacity");
		Check(!full.Add(999.0, 1.0), "reports failure when full rather than overflowing");
		Check(full.Count == MaxCurvePoints, "count stops at capacity");
	}

	std::printf("PlotCurve can hand-draw the Parabola valley\n");
	{
		// The whole point of section 21: reproduce a named system by hand, then
		// go beyond it. Five points approximating Region 20-30, Increment 2.5.
		PlotCurve c;
		c.Add(20.0, 2.5);
		c.Add(22.5, 0.625);
		c.Add(25.0, 0.0);
		c.Add(27.5, 0.625);
		c.Add(30.0, 2.5);
		Near(c.Evaluate(20.0), 2.5, "hand-drawn valley: worst at 20");
		Near(c.Evaluate(25.0), 0.0, "hand-drawn valley: trough at 25");
		Near(c.Evaluate(30.0), 2.5, "hand-drawn valley: worst at 30");
		Check(c.Evaluate(23.0) < c.Evaluate(21.0), "descends into the trough");
		Check(c.Evaluate(29.0) > c.Evaluate(27.0), "climbs back out");
		// And something the named systems cannot express at all: asymmetry.
		PlotCurve skew;
		skew.Add(0.0, 0.0);
		skew.Add(5.0, 3.0);
		skew.Add(30.0, 0.5);
		Check(skew.Evaluate(5.0) > skew.Evaluate(30.0),
			"asymmetric curve: worst at close range, tight far away");
	}

	// ---- Segment easing (section 21.1) ------------------------------------
	std::printf("segment easing\n");
	{
		// Default is Power=1 == linear, so an unannotated curve is unchanged by
		// easing existing at all.
		PlotCurve lin;
		lin.Add(0.0, 0.0);
		lin.Add(10.0, 10.0);
		Near(lin.Evaluate(5.0), 5.0, "default easing is linear");

		// ease-IN: "slowly increase then rapidly spike" -- BELOW the line early.
		PlotCurve in;
		in.Add(0.0, 0.0, SegmentEase::Power, 2.0);
		in.Add(10.0, 10.0);
		Check(in.Evaluate(5.0) < 5.0, "ease-in lags the line at the midpoint");
		Near(in.Evaluate(5.0), 2.5, "ease-in midpoint is t^2");
		Near(in.Evaluate(0.0), 0.0, "ease-in still hits the left endpoint");
		Near(in.Evaluate(10.0), 10.0, "ease-in still hits the right endpoint");

		// ease-OUT: "rapidly increase then slow down" -- ABOVE the line early.
		PlotCurve out;
		out.Add(0.0, 0.0, SegmentEase::Power, 0.5);
		out.Add(10.0, 10.0);
		Check(out.Evaluate(5.0) > 5.0, "ease-out leads the line at the midpoint");
		Near(out.Evaluate(0.0), 0.0, "ease-out still hits the left endpoint");
		Near(out.Evaluate(10.0), 10.0, "ease-out still hits the right endpoint");

		// NOT asserted: that ease-in and ease-out are mirror images. They are
		// not -- the mirror of t^k is 1-(1-t)^k, whereas ease-out here is
		// t^(1/k). Both decelerate, but they are different curves. Claiming
		// symmetry was a wrong prediction on the first run of this test, not a
		// bug in the easing. A single exponent covering (0, inf) is worth more
		// to a modder than exact mirror symmetry: one number, one meaning
		// ("bigger = slower start"), no second concept.
		//
		// What IS true and worth pinning: the whole family stays monotonic and
		// inside the segment's value range.
		for (double r = 0.0; r <= 10.0; r += 0.25)
		{
			for (auto* c : { &in, &out })
			{
				const double v = c->Evaluate(r);
				Check(v >= -1e-9 && v <= 10.0 + 1e-9, "eased segment stays within its endpoints");
			}
		}
		double prevIn = -1.0, prevOut = -1.0;
		for (double r = 0.0; r <= 10.0; r += 0.25)
		{
			const double vi = in.Evaluate(r), vo = out.Evaluate(r);
			Check(vi >= prevIn - 1e-12, "ease-in is monotonic");
			Check(vo >= prevOut - 1e-12, "ease-out is monotonic");
			prevIn = vi; prevOut = vo;
		}
	}

	std::printf("easing works the same on a FALLING segment\n");
	{
		// Easing is defined on the segment's local progress, so "slow at the
		// start of this leg" means the same whether it rises or falls.
		PlotCurve down;
		down.Add(0.0, 10.0, SegmentEase::Power, 2.0);
		down.Add(10.0, 0.0);
		Check(down.Evaluate(5.0) > 5.0, "ease-in on a falling segment stays high early");
		Near(down.Evaluate(5.0), 7.5, "falling ease-in midpoint");
		Near(down.Evaluate(10.0), 0.0, "falling segment still reaches its endpoint");
	}

	std::printf("Smooth and Step\n");
	{
		PlotCurve sm;
		sm.Add(0.0, 0.0, SegmentEase::Smooth, 1.0);
		sm.Add(10.0, 10.0);
		Near(sm.Evaluate(5.0), 5.0, "smoothstep is symmetric: midpoint is on the line");
		Check(sm.Evaluate(2.5) < 2.5, "smoothstep eases in at the start");
		Check(sm.Evaluate(7.5) > 7.5, "smoothstep eases out at the end");
		Near(sm.Evaluate(0.0), 0.0, "smoothstep hits the left endpoint");
		Near(sm.Evaluate(10.0), 10.0, "smoothstep hits the right endpoint");

		PlotCurve st;
		st.Add(0.0, 1.0, SegmentEase::Step, 1.0);
		st.Add(10.0, 5.0);
		Near(st.Evaluate(0.0), 1.0, "step holds at the left value");
		Near(st.Evaluate(9.99), 1.0, "step still holds just before the next point");
		Near(st.Evaluate(10.0), 5.0, "step jumps at the next point");
	}

	std::printf("SetEase targets an existing point\n");
	{
		PlotCurve c;
		c.Add(0.0, 0.0);
		c.Add(10.0, 10.0);
		Check(c.SetEase(0.0, SegmentEase::Power, 2.0), "SetEase finds an existing range");
		Near(c.Evaluate(5.0), 2.5, "SetEase actually changed the segment");
		Check(!c.SetEase(99.0, SegmentEase::Power, 2.0),
			"SetEase reports an orphaned range instead of silently ignoring it");
	}

	std::printf("\n%d checks, %d failures\n", Checks, Failures);
	return Failures == 0 ? 0 : 1;
}
