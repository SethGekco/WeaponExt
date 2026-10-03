#include "Body.h"
#include "../../Magnetron.h"

#include <Utilities/Macro.h>
#include <Utilities/Debug.h>

WarheadTypeExt::ExtContainer WarheadTypeExt::ExtMap;

WarheadTypeExt::ExtContainer::ExtContainer() : Container<WarheadTypeExt>("WarheadTypeClass") { }
WarheadTypeExt::ExtContainer::~ExtContainer() = default;

void WarheadTypeExt::ExtData::LoadFromINIFile(CCINIClass* pINI)
{
	auto pThis = this->OwnerObject();
	const char* section = pThis->ID;

	if (!pINI->GetSection(section))
		return;

	INI_EX exINI(pINI);

	this->Magnetron_Speed.Read(exINI, section, "Magnetron.Speed");
	this->Magnetron_Climb.Read(exINI, section, "Magnetron.Climb");
	this->Magnetron_Crash.Read(exINI, section, "Magnetron.Crash");
	this->Magnetron_Height.Read(exINI, section, "Magnetron.Height");
	this->Magnetron_Accel.Read(exINI, section, "Magnetron.Accel");
	this->Magnetron_Wobbles.Read(exINI, section, "Magnetron.Wobbles");
	this->Magnetron_Deviation.Read(exINI, section, "Magnetron.Deviation");
	this->Magnetron_TurnRate.Read(exINI, section, "Magnetron.TurnRate");

	this->Magnetron_ReleaseOnStop.Read(exINI, section, "Magnetron.ReleaseOnStop");
	this->Magnetron_ReleaseOnStop_Delay.Read(exINI, section, "Magnetron.ReleaseOnStop.Delay");
	this->Magnetron_MaxHoldTime.Read(exINI, section, "Magnetron.MaxHoldTime");

	this->Magnetron_Handoff.Read(exINI, section, "Magnetron.Handoff");
	this->Magnetron_Handoff_OnStopped.Read(exINI, section, "Magnetron.Handoff.OnStopped");
	this->Magnetron_Handoff_OnArrived.Read(exINI, section, "Magnetron.Handoff.OnArrived");
	this->Magnetron_Handoff_OnMindControl.Read(exINI, section, "Magnetron.Handoff.OnMindControl");
	this->Magnetron_Handoff_StoppedFor.Read(exINI, section, "Magnetron.Handoff.StoppedFor");
	this->Magnetron_Handoff_ArriveRange.Read(exINI, section, "Magnetron.Handoff.ArriveRange");
	this->Magnetron_Handoff_Lift.Read(exINI, section, "Magnetron.Handoff.Lift");
	this->Magnetron_Handoff_Crash.Read(exINI, section, "Magnetron.Handoff.Crash");

	if (this->HasAnyMagnetron())
	{
		// Warn loudly about the one configuration we cannot rescue well: a
		// non-jumpjet Locomotor= has no release code of its own anywhere in
		// the engine, so the flight overrides below do not apply to it and
		// only MaxHoldTime/ReleaseOnStop keep the victim from being stuck.
		const bool isJumpjet = Magnetron::WarheadUsesJumpjet(pThis);

		Debug::Log("[WeaponExt] %s: magnetron overrides%s releaseOnStop=%s(%d) "
			"maxHold=%d\n",
			section,
			this->HasFlightOverrides() ? (isJumpjet ? " [flight: OK]"
				: " [flight: IGNORED, Locomotor= is not the jumpjet CLSID]") : "",
			this->Magnetron_ReleaseOnStop ? "yes" : "no",
			this->Magnetron_ReleaseOnStop_Delay.Get(),
			this->Magnetron_MaxHoldTime.Get());

		if (this->Magnetron_Handoff)
		{
			Debug::Log("[WeaponExt]   handoff=yes (lift=%d%s) on:%s%s%s -- the "
				"imbued locomotor travels, then the jumpjet performs the "
				"release.\n",
				this->Magnetron_Handoff_Lift.Get(),
				this->Magnetron_Handoff_Lift == 0 ? ", invisible" : "",
				this->Magnetron_Handoff_OnStopped ? " stopped" : "",
				this->Magnetron_Handoff_OnArrived ? " arrived" : "",
				this->Magnetron_Handoff_OnMindControl ? " mindcontrol" : "");
		}
	}
}

template <typename T>
void WarheadTypeExt::ExtData::Serialize(T& Stm)
{
	Stm
		.Process(this->Magnetron_Speed)
		.Process(this->Magnetron_Climb)
		.Process(this->Magnetron_Crash)
		.Process(this->Magnetron_Height)
		.Process(this->Magnetron_Accel)
		.Process(this->Magnetron_Wobbles)
		.Process(this->Magnetron_Deviation)
		.Process(this->Magnetron_TurnRate)
		.Process(this->Magnetron_ReleaseOnStop)
		.Process(this->Magnetron_ReleaseOnStop_Delay)
		.Process(this->Magnetron_MaxHoldTime)
		.Process(this->Magnetron_Handoff)
		.Process(this->Magnetron_Handoff_OnStopped)
		.Process(this->Magnetron_Handoff_OnArrived)
		.Process(this->Magnetron_Handoff_OnMindControl)
		.Process(this->Magnetron_Handoff_StoppedFor)
		.Process(this->Magnetron_Handoff_ArriveRange)
		.Process(this->Magnetron_Handoff_Lift)
		.Process(this->Magnetron_Handoff_Crash)
		;
}

void WarheadTypeExt::ExtData::LoadFromStream(PhobosStreamReader& Stm)
{
	Extension<WarheadTypeClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void WarheadTypeExt::ExtData::SaveToStream(PhobosStreamWriter& Stm)
{
	Extension<WarheadTypeClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

// ============================================================================
// Container lifecycle. Registers copied from Phobos's own WarheadTypeExt
// hooks, not guessed -- note CTOR uses EBP while SDDTOR and LoadFromINI use
// ESI, and LoadFromINI's INI pointer is at stack 0x150.
//
// 0x75DEAF is deliberately NOT hooked: Phobos documents it as the
// section-does-not-exist return, and a destructive re-parse there can only
// lose data (same trap as Techno-Type-Lifecycle.md).
// ============================================================================

DEFINE_HOOK(0x75D1A9, WarheadTypeClass_CTOR_WeaponExt, 0x7)
{
	GET(WarheadTypeClass*, pItem, EBP);

	WarheadTypeExt::ExtMap.TryAllocate(pItem);

	return 0;
}

DEFINE_HOOK(0x75E5C8, WarheadTypeClass_SDDTOR_WeaponExt, 0x6)
{
	GET(WarheadTypeClass*, pItem, ESI);

	WarheadTypeExt::ExtMap.Remove(pItem);

	return 0;
}

DEFINE_HOOK_AGAIN(0x75E2C0, WarheadTypeClass_SaveLoad_Prefix_WeaponExt, 0x5)
DEFINE_HOOK(0x75E0C0, WarheadTypeClass_SaveLoad_Prefix_WeaponExt, 0x8)
{
	GET_STACK(WarheadTypeClass*, pItem, 0x4);
	GET_STACK(IStream*, pStm, 0x8);

	WarheadTypeExt::ExtMap.PrepareStream(pItem, pStm);

	return 0;
}

DEFINE_HOOK(0x75E2AE, WarheadTypeClass_Load_Suffix_WeaponExt, 0x7)
{
	WarheadTypeExt::ExtMap.LoadStatic();

	return 0;
}

DEFINE_HOOK(0x75E39C, WarheadTypeClass_Save_Suffix_WeaponExt, 0x5)
{
	WarheadTypeExt::ExtMap.SaveStatic();

	return 0;
}

DEFINE_HOOK(0x75DEA0, WarheadTypeClass_LoadFromINI_WeaponExt, 0x5)
{
	GET(WarheadTypeClass*, pItem, ESI);
	GET_STACK(CCINIClass*, pINI, 0x150);

	WarheadTypeExt::ExtMap.LoadFromINI(pItem, pINI);

	return 0;
}
