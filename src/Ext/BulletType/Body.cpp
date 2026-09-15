#include "Body.h"

#include <Utilities/Macro.h>
#include <Utilities/Debug.h>

#include <cstring> // _strcmpi

#include <ScatterExtDiag.h>
#include <CurveParse.h>
#include <WeaponExt.h>

BulletTypeExt::ExtContainer BulletTypeExt::ExtMap;

BulletTypeExt::ExtContainer::ExtContainer() : Container<BulletTypeExt>("BulletTypeClass") { }
BulletTypeExt::ExtContainer::~ExtContainer() = default;

void BulletTypeExt::ExtData::LoadFromINIFile(CCINIClass* pINI)
{
	auto pThis = this->OwnerObject();
	const char* section = pThis->ID;

	if (!pINI->GetSection(section))
		return;

	INI_EX exINI(pINI);

	this->ScatterMinX.Read(exINI, section, "BallisticScatter.Min.X");
	this->ScatterMaxX.Read(exINI, section, "BallisticScatter.Max.X");
	this->ScatterMinY.Read(exINI, section, "BallisticScatter.Min.Y");
	this->ScatterMaxY.Read(exINI, section, "BallisticScatter.Max.Y");
	this->ScatterMinZ.Read(exINI, section, "BallisticScatter.Min.Z");
	this->ScatterMaxZ.Read(exINI, section, "BallisticScatter.Max.Z");

	if (pINI->ReadString(section, "BallisticScatter.Axis", "",
		Phobos::readBuffer, Phobos::readLength))
	{
		if (_strcmpi(Phobos::readBuffer, "Firer") == 0)
			this->AxisFirerRelative = true;
		else if (_strcmpi(Phobos::readBuffer, "World") == 0)
			this->AxisFirerRelative = false;
		else if (*Phobos::readBuffer)
		{
			Debug::Log("[WeaponExt] %s: unrecognised BallisticScatter.Axis=%s "
				"(expected Firer|World); using Firer.\n", section, Phobos::readBuffer);
		}
	}

	{
		int bad = 0; bool full = false;
		int total = 0;
		total += CurveParse::ReadPlotCurve(pINI, section, "BallisticScatter.Max.X",
			this->MaxCurveX, WeaponExtDLL::readBuffer, WeaponExtDLL::readLength, &bad, &full);
		total += CurveParse::ReadPlotCurve(pINI, section, "BallisticScatter.Max.Y",
			this->MaxCurveY, WeaponExtDLL::readBuffer, WeaponExtDLL::readLength, &bad, &full);
		total += CurveParse::ReadPlotCurve(pINI, section, "BallisticScatter.Max.Z",
			this->MaxCurveZ, WeaponExtDLL::readBuffer, WeaponExtDLL::readLength, &bad, &full);
		if (total)
			Debug::Log("[WeaponExt] %s: per-axis plot curves X=%d Y=%d Z=%d point(s)\n",
				section, this->MaxCurveX.Count, this->MaxCurveY.Count, this->MaxCurveZ.Count);
		if (bad)
			Debug::Log("[WeaponExt] %s: %d malformed BallisticScatter.Max.*.At[...] key(s).\n",
				section, bad);
		CurveParse::ReadPlotEasing(pINI, section, "BallisticScatter.Max.X",
			this->MaxCurveX, WeaponExtDLL::readBuffer, WeaponExtDLL::readLength);
		CurveParse::ReadPlotEasing(pINI, section, "BallisticScatter.Max.Y",
			this->MaxCurveY, WeaponExtDLL::readBuffer, WeaponExtDLL::readLength);
		CurveParse::ReadPlotEasing(pINI, section, "BallisticScatter.Max.Z",
			this->MaxCurveZ, WeaponExtDLL::readBuffer, WeaponExtDLL::readLength);
	}

	if (this->HasPerAxis())
	{
		Debug::Log("[WeaponExt] %s: per-axis scatter X[%.2f,%.2f] Y[%.2f,%.2f] Z[%.2f,%.2f] axis=%s\n",
			section,
			this->ScatterMinX.Get(0.0), this->ScatterMaxX.Get(0.0),
			this->ScatterMinY.Get(0.0), this->ScatterMaxY.Get(0.0),
			this->ScatterMinZ.Get(0.0), this->ScatterMaxZ.Get(0.0),
			this->AxisFirerRelative ? "Firer" : "World");
	}
}

template <typename T>
void BulletTypeExt::ExtData::Serialize(T& Stm)
{
	Stm
		.Process(this->ScatterMinX)
		.Process(this->ScatterMaxX)
		.Process(this->ScatterMinY)
		.Process(this->ScatterMaxY)
		.Process(this->ScatterMinZ)
		.Process(this->ScatterMaxZ)
		.Process(this->AxisFirerRelative)
		.Process(this->MaxCurveX)
		.Process(this->MaxCurveY)
		.Process(this->MaxCurveZ)
		;
}

void BulletTypeExt::ExtData::LoadFromStream(PhobosStreamReader& Stm)
{
	Extension<BulletTypeClass>::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void BulletTypeExt::ExtData::SaveToStream(PhobosStreamWriter& Stm)
{
	Extension<BulletTypeClass>::SaveToStream(Stm);
	this->Serialize(Stm);
}

// ============================================================================
// Container lifecycle. Registers/offsets mirror Phobos's own BulletTypeExt
// hooks -- taken from its source rather than guessed. Note the CTOR uses EAX
// here, not ESI as WeaponTypeClass does.
// ============================================================================

DEFINE_HOOK(0x46BDD9, BulletTypeClass_CTOR_ScatterExt, 0x5)
{
	GET(BulletTypeClass*, pItem, EAX);

	BulletTypeExt::ExtMap.TryAllocate(pItem);

	return 0;
}

DEFINE_HOOK(0x46C8B6, BulletTypeClass_SDDTOR_ScatterExt, 0x6)
{
	GET(BulletTypeClass*, pItem, ESI);

	BulletTypeExt::ExtMap.Remove(pItem);

	return 0;
}

DEFINE_HOOK_AGAIN(0x46C730, BulletTypeClass_SaveLoad_Prefix_ScatterExt, 0x8)
DEFINE_HOOK(0x46C6A0, BulletTypeClass_SaveLoad_Prefix_ScatterExt, 0x5)
{
	GET_STACK(BulletTypeClass*, pItem, 0x4);
	GET_STACK(IStream*, pStm, 0x8);

	BulletTypeExt::ExtMap.PrepareStream(pItem, pStm);

	return 0;
}

// Size 5, not Phobos's 0x4. The real bytes here are `5E` (pop esi) +
// `C2 08 00` (ret 8) = 4, followed by `90` padding. Syringe always writes 5
// bytes regardless, so declaring 4 under-reports what is actually clobbered.
// Declaring 5 covers the padding nop and makes the declared window match
// reality; behaviour is identical because the `ret` transfers control before
// the resume address is ever reached.
DEFINE_HOOK(0x46C722, BulletTypeClass_Load_Suffix_ScatterExt, 0x5)
{
	BulletTypeExt::ExtMap.LoadStatic();

	return 0;
}

// Size 5, not Phobos's 0x3 -- same reasoning as Load_Suffix above. The bytes
// are `C2 0C 00` (ret 0xC) followed by two `90` padding nops.
DEFINE_HOOK(0x46C74A, BulletTypeClass_Save_Suffix_ScatterExt, 0x5)
{
	BulletTypeExt::ExtMap.SaveStatic();

	return 0;
}

DEFINE_HOOK(0x46C41C, BulletTypeClass_LoadFromINI_ScatterExt, 0xA)
{
	GET(BulletTypeClass*, pItem, ESI);
	GET_STACK(CCINIClass*, pINI, 0x90);

	ScatterDiag::CountBulletParse();
	BulletTypeExt::ExtMap.LoadFromINI(pItem, pINI);

	return 0;
}
