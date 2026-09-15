#include "Body.h"

#include <Utilities/Macro.h>
#include <Utilities/Debug.h>

TechnoTypeExt::ExtContainer TechnoTypeExt::ExtMap;

TechnoTypeExt::ExtContainer::ExtContainer() : Container<TechnoTypeExt>("TechnoTypeClass") { }
TechnoTypeExt::ExtContainer::~ExtContainer() = default;

void TechnoTypeExt::ExtData::LoadFromINIFile(CCINIClass* pINI)
{
	auto pThis = this->OwnerObject();
	const char* section = pThis->ID;

	if (!pINI->GetSection(section))
		return;

	INI_EX exINI(pINI);

	this->InaccuracyModifier.Read(exINI, section, "InaccuracyModifier");
	this->InaccuracyModifier_Veteran.Read(exINI, section, "InaccuracyModifier.Veteran");
	this->InaccuracyModifier_Elite.Read(exINI, section, "InaccuracyModifier.Elite");

	if (this->HasAny())
	{
		Debug::Log("[WeaponExt] %s: InaccuracyModifier base=%.2f veteran=%.2f elite=%.2f\n",
			section,
			this->InaccuracyModifier.Get(1.0),
			this->InaccuracyModifier_Veteran.Get(this->InaccuracyModifier.Get(1.0)),
			this->InaccuracyModifier_Elite.Get(this->InaccuracyModifier.Get(1.0)));
	}
}

template <typename T>
void TechnoTypeExt::ExtData::Serialize(T& Stm)
{
	Stm
		.Process(this->InaccuracyModifier)
		.Process(this->InaccuracyModifier_Veteran)
		.Process(this->InaccuracyModifier_Elite)
		;
}

void TechnoTypeExt::ExtData::LoadFromStream(PhobosStreamReader& Stm)
{
	Extension<TechnoTypeClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void TechnoTypeExt::ExtData::SaveToStream(PhobosStreamWriter& Stm)
{
	Extension<TechnoTypeClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

// ============================================================================
// Container lifecycle. Registers and offsets taken from Phobos's own
// TechnoTypeExt hooks rather than guessed. Note CTOR uses ESI while DTOR uses
// ECX, and LoadFromINI uses EBP -- three different registers in one family.
// ============================================================================

DEFINE_HOOK(0x711835, TechnoTypeClass_CTOR_ScatterExt, 0x5)
{
	GET(TechnoTypeClass*, pItem, ESI);

	TechnoTypeExt::ExtMap.TryAllocate(pItem);

	return 0;
}

DEFINE_HOOK(0x711AE0, TechnoTypeClass_DTOR_ScatterExt, 0x5)
{
	GET(TechnoTypeClass*, pItem, ECX);

	TechnoTypeExt::ExtMap.Remove(pItem);

	return 0;
}

DEFINE_HOOK_AGAIN(0x716DC0, TechnoTypeClass_SaveLoad_Prefix_ScatterExt, 0x5)
DEFINE_HOOK(0x7162F0, TechnoTypeClass_SaveLoad_Prefix_ScatterExt, 0x6)
{
	GET_STACK(TechnoTypeClass*, pItem, 0x4);
	GET_STACK(IStream*, pStm, 0x8);

	TechnoTypeExt::ExtMap.PrepareStream(pItem, pStm);

	return 0;
}

DEFINE_HOOK(0x716DAC, TechnoTypeClass_Load_Suffix_ScatterExt, 0xA)
{
	TechnoTypeExt::ExtMap.LoadStatic();

	return 0;
}

DEFINE_HOOK(0x717094, TechnoTypeClass_Save_Suffix_ScatterExt, 0x5)
{
	TechnoTypeExt::ExtMap.SaveStatic();

	return 0;
}

DEFINE_HOOK(0x716123, TechnoTypeClass_LoadFromINI_ScatterExt, 0x5)
{
	GET(TechnoTypeClass*, pItem, EBP);
	GET_STACK(CCINIClass*, pINI, 0x380);

	TechnoTypeExt::ExtMap.LoadFromINI(pItem, pINI);

	return 0;
}
