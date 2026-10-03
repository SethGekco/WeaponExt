#pragma once

#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>

#include <WarheadTypeClass.h>

// Magnetron customisation (DESIGN.md section 2, phase M1).
//
// The M0 reverse-engineering changed the plan here, so the short version:
// the Magnetron has no locomotor of its own -- `[LocomotorBeam]` imbues the
// JUMPJET locomotor onto its victim, and JumpjetLocomotionClass keeps its own
// *instance* copies of every flight parameter (Speed, Climb, Crash, Height,
// Accel, Wobbles, Deviation, TurnRate), copied from the victim's TechnoType.
//
// So we do not need to write any flight code: we let vanilla imbue the
// jumpjet exactly as always, then overwrite those instance fields with
// per-warhead values. The jumpjet keeps doing the lifting, carrying, landing
// AND -- critically -- the release, which is the one thing only it knows how
// to do (see Magnetron-Locomotor-Imbue.md).
class WarheadTypeExt
{
public:
	using base_type = WarheadTypeClass;

	static constexpr DWORD Canary = 0x5CA77E03;

	class ExtData final : public Extension<WarheadTypeClass>
	{
	public:
		// Per-weapon flight overrides. Nullable: unset means "leave whatever
		// the engine copied from the victim's own TechnoType", which keeps a
		// warhead carrying none of these tags byte-identical to vanilla.
		Nullable<int> Magnetron_Speed;
		Nullable<double> Magnetron_Climb;
		Nullable<double> Magnetron_Crash;      // descent rate => landing force
		Nullable<int> Magnetron_Height;
		Nullable<double> Magnetron_Accel;
		Nullable<double> Magnetron_Wobbles;
		Nullable<int> Magnetron_Deviation;
		Nullable<int> Magnetron_TurnRate;

		// Release when the beam stops. Vanilla has NO beam-stop detection at
		// all -- the jumpjet decides when it is done -- so this is ours.
		// Measured as "frames since the last fresh imbue": every shot
		// re-imbues, so a gap in imbues IS the beam stopping. (Comparing
		// firer->Target to the victim does NOT work; a magnetron keeps
		// targeting what it holds.)
		Valueable<bool> Magnetron_ReleaseOnStop;
		Valueable<int> Magnetron_ReleaseOnStop_Delay;   // frames without a fresh beam hit; MUST exceed the weapon ROF

		// Hard ceiling on how long a victim may be held, in frames. The
		// safety net for a modder who set `Locomotor=` to a non-jumpjet
		// CLSID: those locomotors contain no release code whatsoever, so
		// without this the victim is paralysed for the rest of the match.
		Valueable<int> Magnetron_MaxHoldTime;

		ExtData(WarheadTypeClass* OwnerObject) : Extension<WarheadTypeClass>(OwnerObject)
			, Magnetron_Speed { }
			, Magnetron_Climb { }
			, Magnetron_Crash { }
			, Magnetron_Height { }
			, Magnetron_Accel { }
			, Magnetron_Wobbles { }
			, Magnetron_Deviation { }
			, Magnetron_TurnRate { }
			, Magnetron_ReleaseOnStop { false }
			, Magnetron_ReleaseOnStop_Delay { 45 }
			, Magnetron_MaxHoldTime { -1 }
		{ }

		virtual ~ExtData() = default;

		virtual void LoadFromINIFile(CCINIClass* pINI) override;
		virtual void Initialize() override { }
		virtual void InvalidatePointer(void* ptr, bool bRemoved) override { }

		virtual void LoadFromStream(PhobosStreamReader& Stm) override;
		virtual void SaveToStream(PhobosStreamWriter& Stm) override;

		bool HasFlightOverrides() const
		{
			return Magnetron_Speed.isset() || Magnetron_Climb.isset()
				|| Magnetron_Crash.isset() || Magnetron_Height.isset()
				|| Magnetron_Accel.isset() || Magnetron_Wobbles.isset()
				|| Magnetron_Deviation.isset() || Magnetron_TurnRate.isset();
		}

		bool HasAnyMagnetron() const
		{
			return HasFlightOverrides() || Magnetron_ReleaseOnStop
				|| Magnetron_MaxHoldTime >= 0;
		}

	private:
		template <typename T>
		void Serialize(T& Stm);
	};

	class ExtContainer final : public Container<WarheadTypeExt>
	{
	public:
		ExtContainer();
		~ExtContainer();
	};

	static ExtContainer ExtMap;
};
