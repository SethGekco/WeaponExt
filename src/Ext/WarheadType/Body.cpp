#include "Body.h"

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

	this->WarheadSize_Exempt.Read(exINI, section, "WarheadSize.Exempt");
	this->WarheadSize_IgnoreSpreadBelow.Read(exINI, section, "WarheadSize.IgnoreSpreadBelow");
	this->WarheadSize_IgnoreSpreadAbove.Read(exINI, section, "WarheadSize.IgnoreSpreadAbove");
	this->WarheadSize_MultiplierCap.Read(exINI, section, "WarheadSize.MultiplierCap");
	this->WarheadSize_MultiplierFloor.Read(exINI, section, "WarheadSize.MultiplierFloor");
	this->WarheadSize_SpreadCap.Read(exINI, section, "WarheadSize.SpreadCap");
	this->WarheadSize_SpreadFloor.Read(exINI, section, "WarheadSize.SpreadFloor");
	this->WarheadSize_FromZero.Read(exINI, section, "WarheadSize.FromZero");

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
