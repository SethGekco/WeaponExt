#include "Bounty.h"
#include "WeaponExt.h"
#include "WeaponExtDiag.h"

#include <TechnoClass.h>
// TechnoClass.h -> Helpers/Cast.h instantiates generic_cast<const FootClass*>,
// which needs FootClass complete. Required even though we never name it.
#include <FootClass.h>
#include <UnitTypeClass.h>
#include <InfantryTypeClass.h>
#include <BuildingTypeClass.h>
#include <AircraftTypeClass.h>
#include <CCINIClass.h>

#include <Ext/TechnoType/Body.h>
#include <Misc/FlyingStrings.h>

#include <Utilities/Debug.h>

#include <cstring>

namespace
{
	constexpr double LeptonsPerCell = 256.0;   // never name this `Leptons`

	// Number of TechnoTypes with Bounty.Leech=yes. Zero in every mod that
	// does not use the feature, which is what keeps the per-kill sweep over
	// TechnoClass::Array off the critical path entirely.
	int LeechTypeCount = 0;

	int DefaultBountyValue = 0;
	double DefaultBountyCostRatio = 0.0;
	double DefaultBountySoylentRatio = 0.0;
	bool BountyDisplay = true;

	// Where the flying text goes, and what object it is anchored to. Set once
	// per kill event so PayHouse does not need them threaded through every
	// call -- there is exactly one victim per event.
	CoordStruct DisplayCoords {};
	ObjectClass* DisplaySource = nullptr;

	TechnoTypeClass* FindTechnoType(const char* pID)
	{
		if (auto const pType = UnitTypeClass::Find(pID))
			return pType;
		if (auto const pType = InfantryTypeClass::Find(pID))
			return pType;
		if (auto const pType = BuildingTypeClass::Find(pID))
			return pType;
		if (auto const pType = AircraftTypeClass::Find(pID))
			return pType;
		return nullptr;
	}

	void PayHouse(HouseClass* pHouse, int amount, const char* reason,
		const char* earner)
	{
		if (!pHouse || amount == 0)
			return;

		pHouse->TransactMoney(amount);

		// The familiar "+$25" over the corpse. Shown only to the house that
		// earned it (AffectedHouse::Owner compares against CurrentPlayer
		// inside AddMoneyString), so you see your own income and not the
		// AI's. FlyingStrings also suppresses it under shroud/fog for free.
		if (BountyDisplay)
		{
			FlyingStrings::AddMoneyString(amount, DisplaySource, pHouse,
				AffectedHouse::Owner, DisplayCoords);
		}

		// The log keeps the house ARRAY INDEX as well as the country name:
		// two players can share a country (two Yuris in one match), and the
		// name alone cannot tell you which one got paid.
		WeaponDiag::PayoutLine(reason, earner,
			pHouse->Type ? pHouse->Type->ID : "<none>", amount,
			pHouse->ArrayIndex, pHouse == HouseClass::CurrentPlayer);
	}
}

void BountyTypeFilter::Read(CCINIClass* pINI, const char* pSection, const char* pKey)
{
	if (!pINI->ReadString(pSection, pKey, "", WeaponExtDLL::readBuffer,
		WeaponExtDLL::readLength))
	{
		return;   // absent or empty: stays open (matches everything)
	}

	bool sawAll = false;
	bool sawRestriction = false;

	char* context = nullptr;
	for (char* token = strtok_s(WeaponExtDLL::readBuffer, ",", &context);
		token;
		token = strtok_s(nullptr, ",", &context))
	{
		// trim
		while (*token == ' ' || *token == '\t')
			++token;
		char* end = token + strlen(token);
		while (end > token && (end[-1] == ' ' || end[-1] == '\t'))
			*--end = '\0';

		if (!*token)
			continue;

		if (!_strcmpi(token, "all") || !_strcmpi(token, "any") || !_strcmpi(token, "*"))
		{
			sawAll = true;
		}
		else if (!_strcmpi(token, "none"))
		{
			this->IsClosed = true;
		}
		else if (!_strcmpi(token, "infantrytypes") || !_strcmpi(token, "infantry"))
		{
			this->ClassMask |= Class_Infantry;
			sawRestriction = true;
		}
		else if (!_strcmpi(token, "vehicletypes") || !_strcmpi(token, "unittypes")
			|| !_strcmpi(token, "vehicles") || !_strcmpi(token, "units"))
		{
			this->ClassMask |= Class_Unit;
			sawRestriction = true;
		}
		else if (!_strcmpi(token, "buildingtypes") || !_strcmpi(token, "buildings")
			|| !_strcmpi(token, "structures"))
		{
			this->ClassMask |= Class_Building;
			sawRestriction = true;
		}
		else if (!_strcmpi(token, "aircrafttypes") || !_strcmpi(token, "aircraft"))
		{
			this->ClassMask |= Class_Aircraft;
			sawRestriction = true;
		}
		else if (auto const pType = FindTechnoType(token))
		{
			this->Types.push_back(pType);
			sawRestriction = true;
		}
		else
		{
			// Reported, not swallowed: an unknown ID silently narrowing the
			// filter is indistinguishable from the payout being broken.
			Debug::Log("[WeaponExt] %s: %s lists unknown TechnoType '%s' "
				"(ignored).\n", pSection, pKey, token);
		}
	}

	if (sawRestriction && !sawAll)
		this->IsOpen = false;
}

void Bounty::ReadDefaults(CCINIClass* pINI)
{
	// Idempotent: re-reading the same section yields the same values, so the
	// repeated INI passes over a type section cannot make these drift.
	DefaultBountyValue = pINI->ReadInteger("General", "Bounty.Value",
		DefaultBountyValue);
	DefaultBountyCostRatio = pINI->ReadDouble("General", "Bounty.CostRatio",
		DefaultBountyCostRatio);
	DefaultBountySoylentRatio = pINI->ReadDouble("General", "Bounty.SoylentRatio",
		DefaultBountySoylentRatio);
	BountyDisplay = pINI->ReadBool("General", "Bounty.Display", BountyDisplay);
}

bool Bounty::DisplayEnabled() { return BountyDisplay; }

int Bounty::DefaultValue() { return DefaultBountyValue; }
double Bounty::DefaultCostRatio() { return DefaultBountyCostRatio; }
double Bounty::DefaultSoylentRatio() { return DefaultBountySoylentRatio; }

bool Bounty::HouseAllowed(AffectedHouse flags, HouseClass* pFrom, HouseClass* pTo)
{
	if (flags == AffectedHouse::All)
		return true;
	if (flags == AffectedHouse::None)
		return false;
	if (!pFrom || !pTo)
		return false;

	if (pFrom == pTo)
		return (flags & AffectedHouse::Owner) != AffectedHouse::None;
	if (pFrom->IsAlliedWith(pTo))
		return (flags & AffectedHouse::Allies) != AffectedHouse::None;

	return (flags & AffectedHouse::Enemies) != AffectedHouse::None;
}

void Bounty::NoteLeechChange(bool oldValue, bool newValue)
{
	if (oldValue == newValue)
		return;

	LeechTypeCount += newValue ? 1 : -1;

	if (LeechTypeCount < 0)
		LeechTypeCount = 0;
}

bool Bounty::AnyLeechTypes()
{
	return LeechTypeCount > 0;
}

void Bounty::OnKill(TechnoClass* pKiller, TechnoClass* pVictim)
{
	if (!pVictim)
		return;

	auto const pVictimType = pVictim->GetTechnoType();
	if (!pVictimType)
		return;

	auto const pVictimExt = TechnoTypeExt::ExtMap.Find(pVictimType);
	if (!pVictimExt)
		return;

	auto const pVictimHouse = pVictim->Owner;

	// Anchor the flying text on the victim, where players expect it.
	DisplayCoords = pVictim->GetCoords();
	DisplaySource = pVictim;

	// The base amount is a property of the victim: a flat value (rank-aware)
	// plus optional ratios of its Cost and Soylent. The three components sum.
	// Anything the type leaves unset falls back to the [General] default, so a
	// mod can make every unit worth a share of its cost with one line.
	//
	// NOTE for modders: buildings usually leave Soylent at 0, so
	// Bounty.SoylentRatio pays nothing on them unless Soylent= is set.
	const bool isVeteran = pVictim->Veterancy.IsVeteran();
	const bool isElite = pVictim->Veterancy.IsElite();

	const int base = pVictimExt->BountyValueForRank(isVeteran, isElite)
		+ Scale(pVictimType->Cost, pVictimExt->BountyCostRatio())
		+ Scale(pVictimType->Soylent, pVictimExt->BountySoylentRatio());

	auto const pKillerHouse = pKiller ? pKiller->Owner : nullptr;

	// ---------------------------------------------------------------- direct
	if (base != 0 && pKiller)
	{
		if (auto const pKillerType = pKiller->GetTechnoType())
		{
			if (auto const pKillerExt = TechnoTypeExt::ExtMap.Find(pKillerType))
			{
				if (pKillerExt->Bounty_Hunter
					&& pKillerExt->Bounty_Victims.Matches(pVictimType)
					&& HouseAllowed(pKillerExt->Bounty_VictimHouses,
						pKillerHouse, pVictimHouse))
				{
					const int amount = Scale(base, pKillerExt->Bounty_Ratio);

					switch (pKillerExt->Bounty_Receiver)
					{
					case BountyReceiver::Killer:
						PayHouse(pKillerHouse, amount, "kill", pKillerType->ID);
						break;

					case BountyReceiver::VictimHouse:
						PayHouse(pVictimHouse, amount, "kill->victim", pKillerType->ID);
						break;

					case BountyReceiver::KillerAllies:
						for (auto const pHouse : HouseClass::Array)
						{
							if (pHouse && !pHouse->Defeated
								&& HouseAllowed(AffectedHouse::Team,
									pKillerHouse, pHouse))
							{
								PayHouse(pHouse, amount, "kill->allies",
									pKillerType->ID);
							}
						}
						break;

					case BountyReceiver::None:
						break;
					}

					// The payer loses the money, so a bounty can be a transfer
					// rather than creation. Default is None (vanilla-style).
					switch (pKillerExt->Bounty_Payer)
					{
					case BountyPayer::Killer:
						PayHouse(pKillerHouse, -amount, "kill-cost", pKillerType->ID);
						break;
					case BountyPayer::Victim:
						PayHouse(pVictimHouse, -amount, "kill-cost", pKillerType->ID);
						break;
					case BountyPayer::None:
						break;
					}
				}
			}
		}
	}

	// ----------------------------------------------------------- death reward
	// Paid by the victim's own type for dying, independent of the base amount
	// and of who (if anyone) gets the direct bounty.
	if (pVictimExt->Bounty_DeathReward != 0)
	{
		const int amount = pVictimExt->Bounty_DeathReward;

		for (auto const pHouse : HouseClass::Array)
		{
			if (pHouse && !pHouse->Defeated
				&& HouseAllowed(pVictimExt->Bounty_DeathReward_Houses,
					pVictimHouse, pHouse))
			{
				PayHouse(pHouse, amount, "death-reward", pVictimType->ID);
			}
		}
	}

	// ----------------------------------------------------------------- leech
	// Any live techno whose type opts in skims a share of this kill, whether
	// or not it was involved. Requires a killer, because the killer-side
	// filters have nothing to test otherwise.
	if (base != 0 && pKiller && AnyLeechTypes())
	{
		auto const pKillerType = pKiller->GetTechnoType();
		auto const victimCoords = pVictim->Location;

		for (auto const pLeech : TechnoClass::Array)
		{
			if (!pLeech || !pLeech->IsAlive || pLeech->InLimbo)
				continue;

			auto const pLeechType = pLeech->GetTechnoType();
			if (!pLeechType)
				continue;

			auto const pLeechExt = TechnoTypeExt::ExtMap.Find(pLeechType);
			if (!pLeechExt || !pLeechExt->Bounty_Leech)
				continue;

			// The killer is never its own leech: it already had its chance at
			// the direct payout above, and paying both would silently double.
			if (pLeech == pKiller)
				continue;

			auto const pLeechHouse = pLeech->Owner;

			if (!pLeechExt->Bounty_Leech_Killers.Matches(pKillerType)
				|| !pLeechExt->Bounty_Leech_Victims.Matches(pVictimType))
			{
				continue;
			}

			if (!HouseAllowed(pLeechExt->Bounty_Leech_KillerHouses,
					pLeechHouse, pKillerHouse)
				|| !HouseAllowed(pLeechExt->Bounty_Leech_VictimHouses,
					pLeechHouse, pVictimHouse))
			{
				continue;
			}

			// Negative range means the whole map, per Rex: "range -1 mean
			// everywhere", which makes global and local one mechanism.
			const double range = pLeechExt->Bounty_Leech_Range;
			if (range >= 0.0)
			{
				auto const leechCoords = pLeech->GetCoords();
				const double dx = static_cast<double>(leechCoords.X - victimCoords.X);
				const double dy = static_cast<double>(leechCoords.Y - victimCoords.Y);
				const double dz = static_cast<double>(leechCoords.Z - victimCoords.Z);
				const double limit = range * LeptonsPerCell;

				if (dx * dx + dy * dy + dz * dz > limit * limit)
					continue;
			}

			PayHouse(pLeechHouse, Scale(base, pLeechExt->Bounty_Leech_Ratio),
				"leech", pLeechType->ID);
		}
	}
}
