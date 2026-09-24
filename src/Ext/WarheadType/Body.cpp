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
	this->WarheadSize_Min.Read(exINI, section, "WarheadSize.Min");
	this->WarheadSize_Max.Read(exINI, section, "WarheadSize.Max");
	this->WarheadSize_FromZero.Read(exINI, section, "WarheadSize.FromZero");

	if (this->WarheadSize_Min.isset() && this->WarheadSize_Max.isset()
		&& this->WarheadSize_Min.Get() > this->WarheadSize_Max.Get())
	{
		Debug::Log("[WeaponExt] %s: WarheadSize.Min=%.2f > WarheadSize.Max=%.2f; "
			"Max wins.\n", section, this->WarheadSize_Min.Get(), this->WarheadSize_Max.Get());
	}

	if (this->WarheadSize_Exempt || this->WarheadSize_Min.isset()
		|| this->WarheadSize_Max.isset() || this->WarheadSize_FromZero > 0.0)
	{
		Debug::Log("[WeaponExt] %s: WarheadSize exempt=%d min=%.2f max=%.2f fromZero=%.2f\n",
			section, (int)this->WarheadSize_Exempt.Get(),
			this->WarheadSize_Min.Get(-1.0), this->WarheadSize_Max.Get(-1.0),
			this->WarheadSize_FromZero.Get());
	}
}

template <typename T>
void WarheadTypeExt::ExtData::Serialize(T& Stm)
{
	Stm
		.Process(this->WarheadSize_Exempt)
		.Process(this->WarheadSize_Min)
		.Process(this->WarheadSize_Max)
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
