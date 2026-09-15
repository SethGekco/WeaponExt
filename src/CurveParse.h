#pragma once

#include <CCINIClass.h>

#include <Curve.h>

#include <cstdlib>
#include <cstring>
#include <cstdio>

// Parses `<prefix>.At[<range>]=<value>` plot points out of an INI section
// (DESIGN.md section 21).
//
// This needs KEY ENUMERATION rather than a plain read, because the bracket
// carries data: we cannot ask for a key whose name we do not already know.
// CCINIClass::GetKeyCount / GetKeyName provide it, and this runs once per
// weapon at load time, never per shot.
//
// Deliberately tolerant of ordering and whitespace, and deliberately NOT
// tolerant of malformed brackets: a key that looks like a plot point but does
// not parse is reported, not silently ignored. A tag that is accepted and then
// does nothing is the worst outcome (cf. BallisticScatter.Min.Z, which was
// parsed and then unused for a whole phase).
namespace CurveParse
{
	// `buf` is caller-supplied scratch so this header stays free of statics.
	// Returns the number of points added; `outMalformed` counts keys that
	// matched the prefix and `.At[` but whose bracket or value was unparseable.
	inline int ReadPlotCurve(CCINIClass* pINI, const char* section, const char* prefix,
		PlotCurve& out, char* buf, size_t bufLen, int* outMalformed = nullptr,
		bool* outOverflowed = nullptr)
	{
		if (outMalformed) *outMalformed = 0;
		if (outOverflowed) *outOverflowed = false;

		if (!pINI || !section || !prefix || !pINI->GetSection(section))
			return 0;

		const size_t plen = std::strlen(prefix);
		const int keyCount = pINI->GetKeyCount(section);
		int added = 0;

		for (int i = 0; i < keyCount; ++i)
		{
			const char* key = pINI->GetKeyName(section, i);
			if (!key)
				continue;

			if (_strnicmp(key, prefix, plen) != 0)
				continue;

			// The prefix alone is not enough: `RangeScatter` also prefixes
			// `RangeScatter.Region`. The `.At[` is what identifies a plot point.
			const char* rest = key + plen;
			if (_strnicmp(rest, ".At[", 4) != 0)
				continue;

			rest += 4;

			char* end = nullptr;
			const double range = std::strtod(rest, &end);
			if (end == rest)
			{
				if (outMalformed) ++*outMalformed;
				continue;   // nothing numeric inside the bracket
			}

			while (*end == ' ' || *end == '\t')
				++end;

			if (*end != ']')
			{
				if (outMalformed) ++*outMalformed;
				continue;   // unterminated bracket
			}

			if (!pINI->ReadString(section, key, "", buf, bufLen))
				continue;

			double value = 0.0;
			if (std::sscanf(buf, "%lf", &value) != 1)
			{
				if (outMalformed) ++*outMalformed;
				continue;
			}

			if (!out.Add(range, value))
			{
				// Full. Stop rather than silently dropping the rest.
				if (outOverflowed) *outOverflowed = true;
				break;
			}

			++added;
		}

		return added;
	}

	// Parses `<prefix>.At[<range>].Ease=<name>` and `.At[<range>].Power=<n>`
	// (DESIGN.md 21.1). Run AFTER ReadPlotCurve so every point already exists.
	//
	// Named shapes map onto the same Power exponent, so a casual modder writes
	// EaseIn and an advanced one writes Power=2.7 -- one mechanism, two levels
	// of depth. Smooth and Step are genuinely different curves and get their
	// own kinds.
	inline int ReadPlotEasing(CCINIClass* pINI, const char* section, const char* prefix,
		PlotCurve& out, char* buf, size_t bufLen, int* outOrphaned = nullptr)
	{
		if (outOrphaned) *outOrphaned = 0;

		if (!pINI || !section || !prefix || !pINI->GetSection(section) || !out.IsSet())
			return 0;

		const size_t plen = std::strlen(prefix);
		const int keyCount = pINI->GetKeyCount(section);
		int applied = 0;

		for (int i = 0; i < keyCount; ++i)
		{
			const char* key = pINI->GetKeyName(section, i);
			if (!key || _strnicmp(key, prefix, plen) != 0)
				continue;

			const char* rest = key + plen;
			if (_strnicmp(rest, ".At[", 4) != 0)
				continue;

			rest += 4;
			char* end = nullptr;
			const double range = std::strtod(rest, &end);
			if (end == rest)
				continue;

			while (*end == ' ' || *end == '\t') ++end;
			if (*end != ']')
				continue;
			++end;

			const bool isEase = _strcmpi(end, ".Ease") == 0;
			const bool isPower = _strcmpi(end, ".Power") == 0;
			if (!isEase && !isPower)
				continue;   // the bare point, or some other suffix

			if (!pINI->ReadString(section, key, "", buf, bufLen) || !*buf)
				continue;

			SegmentEase kind = SegmentEase::Power;
			double power = 1.0;

			if (isPower)
			{
				if (std::sscanf(buf, "%lf", &power) != 1)
					continue;
			}
			else if (_strcmpi(buf, "Linear") == 0)      { power = 1.0; }
			else if (_strcmpi(buf, "EaseIn") == 0)      { power = 2.0; }
			else if (_strcmpi(buf, "EaseOut") == 0)     { power = 0.5; }
			else if (_strcmpi(buf, "SharpIn") == 0)     { power = 4.0; }
			else if (_strcmpi(buf, "SharpOut") == 0)    { power = 0.25; }
			else if (_strcmpi(buf, "Smooth") == 0)      { kind = SegmentEase::Smooth; }
			else if (_strcmpi(buf, "Step") == 0
				|| _strcmpi(buf, "Hold") == 0)          { kind = SegmentEase::Step; }
			else
				continue;   // unrecognised name; caller's parse log will not claim it worked

			// An easing key for a range with no matching point is a typo worth
			// reporting -- silently ignoring it is how a modder loses an hour.
			if (out.SetEase(range, kind, power))
				++applied;
			else if (outOrphaned)
				++*outOrphaned;
		}

		return applied;
	}
}
