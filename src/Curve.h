#pragma once

#include <cmath>

// Shared curve engine for the range-driven features (DESIGN.md 4 and 6).
//
// Everything here is pure: same inputs -> same output, no RNG, no global state.
// That matters because these values feed decisions that must be identical on
// every client (see the desync note in DESIGN.md 10).

enum class ScatterSystem
{
	Line = 0,
	Exponential,
	Parabola,
	Cubic,
};

enum class IncrementMode
{
	Peak = 0,   // Increment is the value reached at the curve's peak (bounded)
	Rate,       // Increment is a per-cell growth coefficient (unbounded)
};

namespace Curve
{
	// Steepness of the Exponential system. Fixed rather than exposed: the
	// shape knob is `System`, the size knob is `Increment`. Adding a third
	// would make the tag set harder to reason about for no new expressiveness.
	inline constexpr double ExponentialK = 2.0;

	// Normalised position within the region, clamped to [0,1].
	inline double Progress(double distance, double regionMin, double regionMax)
	{
		const double span = regionMax - regionMin;
		if (span <= 0.0)
			return 0.0;

		const double t = (distance - regionMin) / span;
		return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
	}

	// shape(t) in [0,1]. See the table in DESIGN.md 4.
	inline double Shape(ScatterSystem system, double t)
	{
		switch (system)
		{
		case ScatterSystem::Line:
			return t;

		case ScatterSystem::Exponential:
			// Normalised so shape(0)=0 and shape(1)=1 regardless of K.
			return (std::exp(ExponentialK * t) - 1.0) / (std::exp(ExponentialK) - 1.0);

		case ScatterSystem::Parabola:
			// The valley: worst at both ends, near-perfect mid-region. This is
			// deliberate and is the reason Parabola is its own system rather
			// than a steepness parameter -- see DESIGN.md 4.
			return (2.0 * t - 1.0) * (2.0 * t - 1.0);

		case ScatterSystem::Cubic:
			return t * t * t;
		}

		return 0.0;
	}

	// Exponent used by Rate mode, mirroring each system's growth order.
	inline double RateExponent(ScatterSystem system)
	{
		switch (system)
		{
		case ScatterSystem::Line:        return 1.0;
		case ScatterSystem::Exponential: return 2.0;
		case ScatterSystem::Cubic:       return 3.0;
		case ScatterSystem::Parabola:    return 1.0; // unreachable, see Evaluate
		}

		return 1.0;
	}

	// The magnitude for this shot, in whatever unit `increment` is expressed in.
	//
	// Returns 0 below regionMin -- the accurate zone is exact, not merely small,
	// so a weapon inside its comfortable band behaves precisely as vanilla.
	//
	// Parabola has no sensible unbounded form (its valley is defined by being
	// normalised to the region), so Rate silently falls back to Peak for it.
	// Callers that want to warn the modder should check for that combination at
	// parse time, not here.
	inline double Evaluate(ScatterSystem system, IncrementMode mode, double increment,
		double distance, double regionMin, double regionMax)
	{
		if (distance < regionMin)
			return 0.0;

		if (mode == IncrementMode::Rate && system != ScatterSystem::Parabola)
			return increment * std::pow(distance - regionMin, RateExponent(system));

		return increment * Shape(system, Progress(distance, regionMin, regionMax));
	}
}

// ---------------------------------------------------------------------------
// User-drawn curves (DESIGN.md section 21)
//
// A piecewise-linear curve defined by (range, value) plot points, so a modder
// can draw any shape instead of choosing from the four named systems. Linear
// between points, CLAMPED outside them -- extrapolating past the ends would let
// a curve run away to absurd values from a typo, and clamping is what someone
// drawing a graph expects the edges to do.
//
// Pure and allocation-free: evaluated once per shot inside TechnoClass::Fire,
// and testable natively with no game.
// ---------------------------------------------------------------------------

// How the segment STARTING at a point reaches the next one (DESIGN.md 21.1).
//
// The two shapes people usually mean, and what to call them:
//   - "slowly increase then rapidly spike"  = ease-IN  = accelerating = Power > 1
//   - "rapidly increase then slow down"     = ease-OUT = decelerating = Power < 1
//
// Defined on the segment's local progress, so they behave the same whether the
// segment rises or falls: ease-in is always "slow at the start of THIS leg".
enum class SegmentEase : int
{
	Power = 0,    // t^Power. 1 = linear, >1 accelerates, <1 decelerates.
	Smooth = 1,   // smoothstep: eased at BOTH ends, an S between the points.
	Step = 2,     // hold this value until the next point, then jump.
};

struct CurvePoint
{
	double Range;
	double Value;
	SegmentEase Ease;
	double Power;   // only consulted when Ease == Power
};

// Fixed capacity: no allocation in a hot path, and a curve needing more than
// this many points is almost certainly a mistake.
inline constexpr int MaxCurvePoints = 16;

struct PlotCurve
{
	CurvePoint Points[MaxCurvePoints] {};
	int Count = 0;

	bool IsSet() const { return Count > 0; }

	// Inserts keeping Points sorted by Range. Sorting here rather than trusting
	// file order matters: INI keys arrive in whatever order they were written,
	// and `At[30]` above `At[20]` must still describe the same curve.
	//
	// A duplicate Range overwrites -- last one wins. Returns false only when
	// the curve is full, so the caller can warn.
	bool Add(double range, double value,
		SegmentEase ease = SegmentEase::Power, double power = 1.0)
	{
		for (int i = 0; i < Count; ++i)
		{
			if (Points[i].Range == range)
			{
				Points[i].Value = value;
				Points[i].Ease = ease;
				Points[i].Power = power;
				return true;
			}
		}

		if (Count >= MaxCurvePoints)
			return false;

		int at = Count;
		while (at > 0 && Points[at - 1].Range > range)
		{
			Points[at] = Points[at - 1];
			--at;
		}

		Points[at] = { range, value, ease, power };
		++Count;
		return true;
	}

	// Sets the easing of the segment starting at an existing point. Separate
	// from Add because the INI gives `At[20]=2.5` and `At[20].Ease=EaseIn` as
	// two independent keys that can arrive in either order.
	bool SetEase(double range, SegmentEase ease, double power)
	{
		for (int i = 0; i < Count; ++i)
		{
			if (Points[i].Range == range)
			{
				Points[i].Ease = ease;
				Points[i].Power = power;
				return true;
			}
		}
		return false;   // no such point: caller should warn
	}

	// Applies the segment's easing to its local progress.
	static double EaseT(const CurvePoint& seg, double t)
	{
		switch (seg.Ease)
		{
		case SegmentEase::Step:
			return 0.0;   // hold the left value; the jump happens at the next point

		case SegmentEase::Smooth:
			return t * t * (3.0 - 2.0 * t);

		case SegmentEase::Power:
		default:
			// Power 1 is linear and is the default, so an unannotated curve
			// behaves exactly as it did before easing existed.
			return seg.Power == 1.0 ? t : std::pow(t, seg.Power);
		}
	}

	double Evaluate(double range) const
	{
		if (Count <= 0)
			return 0.0;

		// One point is a constant, which is a legitimate thing to write.
		if (Count == 1 || range <= Points[0].Range)
			return Points[0].Value;

		if (range >= Points[Count - 1].Range)
			return Points[Count - 1].Value;

		for (int i = 1; i < Count; ++i)
		{
			if (range <= Points[i].Range)
			{
				const auto& a = Points[i - 1];
				const auto& b = Points[i];
				const double span = b.Range - a.Range;
				if (span <= 0.0)
					return b.Value;   // coincident points: take the later one
				const double t = (range - a.Range) / span;
				return a.Value + (b.Value - a.Value) * EaseT(a, t);
			}
		}

		return Points[Count - 1].Value;
	}
};
