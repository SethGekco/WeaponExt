#include "Body.h"

#include <Utilities/Macro.h>
#include <Utilities/Debug.h>

#include <WeaponExt.h>

#include <cstring> // strtok, _strcmpi

namespace
{
	// "owner,allies" -> bitmask. Unknown tokens are logged and ignored; an
	// empty or all-unknown list keeps the previous value.
	void ReadHouses(CCINIClass* pINI, const char* section, const char* key, Valueable<int>& out)
	{
		if (!pINI->ReadString(section, key, "", WeaponExtDLL::readBuffer, WeaponExtDLL::readLength))
			return;

		int mask = 0;
		bool any = false;
		for (char* tok = strtok(WeaponExtDLL::readBuffer, ", \t"); tok; tok = strtok(nullptr, ", \t"))
		{
			any = true;
			if (_strcmpi(tok, "owner") == 0)
				mask |= WarheadSizeHouse_Owner;
			else if (_strcmpi(tok, "allies") == 0)
				mask |= WarheadSizeHouse_Allies;
			else if (_strcmpi(tok, "team") == 0)
				mask |= WarheadSizeHouse_Owner | WarheadSizeHouse_Allies;
			else if (_strcmpi(tok, "enemies") == 0)
				mask |= WarheadSizeHouse_Enemies;
			else if (_strcmpi(tok, "all") == 0)
				mask |= WarheadSizeHouse_All;
			else if (_strcmpi(tok, "none") == 0)
				mask |= 0;
			else
				Debug::Log("[WeaponExt] %s: unrecognised %s token '%s' "
					"(expected owner|allies|team|enemies|all|none); ignored.\n", section, key, tok);
		}

		if (any)
			out = mask;
	}
}

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

	this->WarheadSize_Exempt.Read(exINI, section, "WarheadSize.Exempt");
	this->WarheadSize_IgnoreSpreadBelow.Read(exINI, section, "WarheadSize.IgnoreSpreadBelow");
	this->WarheadSize_IgnoreSpreadAbove.Read(exINI, section, "WarheadSize.IgnoreSpreadAbove");
	this->WarheadSize_MultiplierCap.Read(exINI, section, "WarheadSize.MultiplierCap");
	this->WarheadSize_MultiplierFloor.Read(exINI, section, "WarheadSize.MultiplierFloor");
	this->WarheadSize_SpreadCap.Read(exINI, section, "WarheadSize.SpreadCap");
	this->WarheadSize_SpreadFloor.Read(exINI, section, "WarheadSize.SpreadFloor");
	this->WarheadSize_FromZero.Read(exINI, section, "WarheadSize.FromZero");
	this->WarheadSize_Overflow.Read(exINI, section, "WarheadSize.Overflow");

	this->WarheadSize_Attach.Read(exINI, section, "WarheadSize.Attach");
	this->WarheadSize_Attach_Duration.Read(exINI, section, "WarheadSize.Attach.Duration");
	ReadHouses(pINI, section, "WarheadSize.Attach.Houses", this->WarheadSize_Attach_Houses);
	this->WarheadSize_AnimList_Scaled.Read(exINI, section, "WarheadSize.AnimList.Scaled");
	this->WarheadSize_AnimList_Threshold.Read(exINI, section, "WarheadSize.AnimList.Threshold");
	this->WarheadSize_AnimScale.Read(exINI, section, "WarheadSize.AnimScale");
	this->WarheadSize_AnimScale_Max.Read(exINI, section, "WarheadSize.AnimScale.Max");

	if (this->WarheadSize_Attach.isset() && this->WarheadSize_Attach_Duration <= 0)
	{
		Debug::Log("[WeaponExt] %s: WarheadSize.Attach is set but WarheadSize.Attach.Duration "
			"is not > 0; nothing will be attached.\n", section);
	}

	if (this->HasAttach())
	{
		Debug::Log("[WeaponExt] %s: WarheadSize.Attach=%.2f for %d frames, houses=0x%X\n",
			section, this->WarheadSize_Attach.Get(), this->WarheadSize_Attach_Duration.Get(),
			this->WarheadSize_Attach_Houses.Get());
	}

	if (!this->WarheadSize_AnimList_Scaled.empty())
	{
		Debug::Log("[WeaponExt] %s: WarheadSize.AnimList.Scaled has %d anim(s), threshold=%.2f "
			"(-1 = any enlargement)\n", section, (int)this->WarheadSize_AnimList_Scaled.size(),
			this->WarheadSize_AnimList_Threshold.Get(-1.0));
	}

	if (this->HasAnyWarheadSize())
	{
		// -1 in the log means "not set here, [CombatDamage] applies".
		Debug::Log("[WeaponExt] %s: WarheadSize exempt=%d ignoreBelow=%.2f ignoreAbove=%.2f "
			"multCap=%.2f multFloor=%.2f spreadCap=%.2f spreadFloor=%.2f fromZero=%.2f\n",
			section, (int)this->WarheadSize_Exempt.Get(),
			this->WarheadSize_IgnoreSpreadBelow.Get(-1.0), this->WarheadSize_IgnoreSpreadAbove.Get(-1.0),
			this->WarheadSize_MultiplierCap.Get(-1.0), this->WarheadSize_MultiplierFloor.Get(-1.0),
			this->WarheadSize_SpreadCap.Get(-1.0), this->WarheadSize_SpreadFloor.Get(-1.0),
			this->WarheadSize_FromZero.Get());
	}
}

template <typename T>
void WarheadTypeExt::ExtData::Serialize(T& Stm)
{
	Stm
		.Process(this->WarheadSize_Exempt)
		.Process(this->WarheadSize_IgnoreSpreadBelow)
		.Process(this->WarheadSize_IgnoreSpreadAbove)
		.Process(this->WarheadSize_MultiplierCap)
		.Process(this->WarheadSize_MultiplierFloor)
		.Process(this->WarheadSize_SpreadCap)
		.Process(this->WarheadSize_SpreadFloor)
		.Process(this->WarheadSize_FromZero)
		.Process(this->WarheadSize_Overflow)
		.Process(this->WarheadSize_Attach)
		.Process(this->WarheadSize_Attach_Duration)
		.Process(this->WarheadSize_Attach_Houses)
		.Process(this->WarheadSize_AnimList_Scaled)
		.Process(this->WarheadSize_AnimList_Threshold)
		.Process(this->WarheadSize_AnimScale)
		.Process(this->WarheadSize_AnimScale_Max)
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
// Container lifecycle. Addresses, sizes and registers copied from Phobos's own
// WarheadTypeExt hooks (src/Ext/WarheadType/Body.cpp) rather than guessed.
// Phobos registers NO save/load hooks for WarheadTypeClass, so neither do we:
// everything here is INI data. (Verify in-game that the tags survive a save
// load -- DESIGN.md section 9.6.)
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

DEFINE_HOOK(0x75DEA0, WarheadTypeClass_LoadFromINI_WeaponExt, 0x5)
{
	GET(WarheadTypeClass*, pItem, ESI);
	GET_STACK(CCINIClass*, pINI, 0x150);

	WarheadTypeExt::ExtMap.LoadFromINI(pItem, pINI);

	return 0;
}
