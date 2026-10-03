#include "Body.h"
#include "../../WeaponExt.h"

#include <Utilities/Macro.h>
#include <Utilities/Debug.h>

#include <cstring>

TechnoTypeExt::ExtContainer TechnoTypeExt::ExtMap;

namespace
{
	// Receiver/Payer are small closed vocabularies, so they are read as plain
	// strings rather than given a TemplateDef parser. An unrecognised value is
	// REPORTED and the default kept -- a silent fallback here would look
	// exactly like the feature not working.
	void ReadReceiver(CCINIClass* pINI, const char* pSection, const char* pKey,
		BountyReceiver& value)
	{
		if (!pINI->ReadString(pSection, pKey, "", WeaponExtDLL::readBuffer,
			WeaponExtDLL::readLength))
		{
			return;
		}

		const char* v = WeaponExtDLL::readBuffer;

		if (!_strcmpi(v, "killer") || !_strcmpi(v, "owner"))
			value = BountyReceiver::Killer;
		else if (!_strcmpi(v, "victimhouse") || !_strcmpi(v, "victim"))
			value = BountyReceiver::VictimHouse;
		else if (!_strcmpi(v, "killerallies") || !_strcmpi(v, "allies"))
			value = BountyReceiver::KillerAllies;
		else if (!_strcmpi(v, "none"))
			value = BountyReceiver::None;
		else
		{
			Debug::Log("[WeaponExt] %s: %s=%s is not a recognised receiver "
				"(killer|victimHouse|killerAllies|none); keeping default.\n",
				pSection, pKey, v);
		}
	}

	void ReadPayer(CCINIClass* pINI, const char* pSection, const char* pKey,
		BountyPayer& value)
	{
		if (!pINI->ReadString(pSection, pKey, "", WeaponExtDLL::readBuffer,
			WeaponExtDLL::readLength))
		{
			return;
		}

		const char* v = WeaponExtDLL::readBuffer;

		if (!_strcmpi(v, "none"))
			value = BountyPayer::None;
		else if (!_strcmpi(v, "killer"))
			value = BountyPayer::Killer;
		else if (!_strcmpi(v, "victim"))
			value = BountyPayer::Victim;
		else
		{
			Debug::Log("[WeaponExt] %s: %s=%s is not a recognised payer "
				"(none|killer|victim); keeping default.\n", pSection, pKey, v);
		}
	}
}

TechnoTypeExt::ExtContainer::ExtContainer() : Container<TechnoTypeExt>("TechnoTypeClass") { }
TechnoTypeExt::ExtContainer::~ExtContainer() = default;

void TechnoTypeExt::ExtData::LoadFromINIFile(CCINIClass* pINI)
{
	auto pThis = this->OwnerObject();
	const char* section = pThis->ID;

	if (!pINI->GetSection(section))
		return;

	INI_EX exINI(pINI);

	// Mod-wide bounty fallbacks live in [General]; read them off the same INI
	// we are already parsing rather than adding a RulesClass hook.
	Bounty::ReadDefaults(pINI);

	this->InaccuracyModifier.Read(exINI, section, "InaccuracyModifier");
	this->InaccuracyModifier_Veteran.Read(exINI, section, "InaccuracyModifier.Veteran");
	this->InaccuracyModifier_Elite.Read(exINI, section, "InaccuracyModifier.Elite");

	// --- bounty: victim side ---
	this->Bounty_Value.Read(exINI, section, "Bounty.Value");
	this->Bounty_Value_Veteran.Read(exINI, section, "Bounty.Value.Veteran");
	this->Bounty_Value_Elite.Read(exINI, section, "Bounty.Value.Elite");
	this->Bounty_CostRatio.Read(exINI, section, "Bounty.CostRatio");
	this->Bounty_SoylentRatio.Read(exINI, section, "Bounty.SoylentRatio");
	this->Bounty_DeathReward.Read(exINI, section, "Bounty.DeathReward");
	this->Bounty_DeathReward_Houses.Read(exINI, section, "Bounty.DeathReward.Houses");

	// --- bounty: killer side ---
	this->Bounty_Hunter.Read(exINI, section, "Bounty.Hunter");
	this->Bounty_Ratio.Read(exINI, section, "Bounty.Ratio");
	this->Bounty_Victims.Read(pINI, section, "Bounty.Victims");
	this->Bounty_VictimHouses.Read(exINI, section, "Bounty.VictimHouses");
	ReadReceiver(pINI, section, "Bounty.Receiver", this->Bounty_Receiver);
	ReadPayer(pINI, section, "Bounty.Payer", this->Bounty_Payer);

	// --- bounty: leeching. Maintain the fast-path count with an old/new diff
	// so repeated (and destructive) INI passes over the same section cannot
	// drift it -- see the Encyclopedia's Techno-Type-Lifecycle notes.
	const bool wasLeech = this->Bounty_Leech;
	this->Bounty_Leech.Read(exINI, section, "Bounty.Leech");
	Bounty::NoteLeechChange(wasLeech, this->Bounty_Leech);

	this->Bounty_Leech_Range.Read(exINI, section, "Bounty.Leech.Range");
	this->Bounty_Leech_Ratio.Read(exINI, section, "Bounty.Leech.Ratio");
	this->Bounty_Leech_Killers.Read(pINI, section, "Bounty.Leech.Killers");
	this->Bounty_Leech_KillerHouses.Read(exINI, section, "Bounty.Leech.KillerHouses");
	this->Bounty_Leech_Victims.Read(pINI, section, "Bounty.Leech.Victims");
	this->Bounty_Leech_VictimHouses.Read(exINI, section, "Bounty.Leech.VictimHouses");

	// The single most likely reason a correctly-written leech/hunter pays
	// nothing: the VICTIMS are worth nothing. Say so at load rather than
	// letting it look like the feature is broken.
	if ((this->Bounty_Leech || this->Bounty_Hunter)
		&& Bounty::DefaultValue() == 0
		&& Bounty::DefaultCostRatio() == 0.0
		&& Bounty::DefaultSoylentRatio() == 0.0)
	{
		Debug::Log("[WeaponExt] %s: bounty is enabled here, but [General] sets "
			"no Bounty.Value/CostRatio/SoylentRatio, so unless each victim "
			"defines its own value every payout computes to 0. Add e.g. "
			"`[General] Bounty.CostRatio=1.0`.\n", section);
	}

	if (this->HasAnyBounty())
	{
		Debug::Log("[WeaponExt] %s: bounty value=%d cost*%.2f soylent*%.2f "
			"hunter=%s ratio=%.2f deathReward=%d leech=%s range=%.1f leechRatio=%.2f\n",
			section,
			this->BountyValueForRank(false, false),
			this->BountyCostRatio(),
			this->BountySoylentRatio(),
			this->Bounty_Hunter ? "yes" : "no",
			this->Bounty_Ratio.Get(),
			this->Bounty_DeathReward.Get(),
			this->Bounty_Leech ? "yes" : "no",
			this->Bounty_Leech_Range.Get(),
			this->Bounty_Leech_Ratio.Get());
	}

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
		.Process(this->Bounty_Value)
		.Process(this->Bounty_Value_Veteran)
		.Process(this->Bounty_Value_Elite)
		.Process(this->Bounty_CostRatio)
		.Process(this->Bounty_SoylentRatio)
		.Process(this->Bounty_DeathReward)
		.Process(this->Bounty_DeathReward_Houses)
		.Process(this->Bounty_Hunter)
		.Process(this->Bounty_Ratio)
		.Process(this->Bounty_VictimHouses)
		.Process(this->Bounty_Receiver)
		.Process(this->Bounty_Payer)
		.Process(this->Bounty_Leech)
		.Process(this->Bounty_Leech_Range)
		.Process(this->Bounty_Leech_Ratio)
		.Process(this->Bounty_Leech_KillerHouses)
		.Process(this->Bounty_Leech_VictimHouses)
		;

	// Filters carry a std::vector, so they serialize themselves rather than
	// being memcpy'd by the default handler.
	this->Bounty_Victims.Serialize(Stm);
	this->Bounty_Leech_Killers.Serialize(Stm);
	this->Bounty_Leech_Victims.Serialize(Stm);
}

void TechnoTypeExt::ExtData::LoadFromStream(PhobosStreamReader& Stm)
{
	Extension<TechnoTypeClass>::LoadFromStream(Stm);

	// Deserializing overwrites Bounty_Leech without going through
	// LoadFromINIFile, so the fast-path count has to be re-diffed here or a
	// loaded savegame could skip leech payouts entirely.
	const bool wasLeech = this->Bounty_Leech;

	this->Serialize(Stm);

	Bounty::NoteLeechChange(wasLeech, this->Bounty_Leech);
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
