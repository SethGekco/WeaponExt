#include "WeaponExt.h"
#include "WeaponExtDiag.h"
#include "ScatterExtDiag.h"

#include <Phobos.h>
#include <Syringe.h>
#include <Utilities/Patch.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

HANDLE WeaponExtDLL::hInstance = nullptr;

char WeaponExtDLL::readBuffer[WeaponExtDLL::readLength];
wchar_t WeaponExtDLL::wideBuffer[WeaponExtDLL::readLength];

void WeaponExtDLL::ExeRun()
{
	// Latched: called from both init sites below because the first is
	// contested and may be starved; static patches must not apply twice.
	static bool applied = false;
	if (!applied)
	{
		applied = true;
		Patch::ApplyStatic();
	}
}

bool __stdcall DllMain(HANDLE hInstance, DWORD dwReason, LPVOID)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		WeaponExtDLL::hInstance = hInstance;
		Phobos::hInstance = hInstance; // needed by Patch::ApplyStatic
	}
	return true;
}

SYRINGE_HANDSHAKE(pInfo)
{
	pInfo->Message = const_cast<char*>("WeaponExt");
	return S_OK;
}

// ---------------------------------------------------------------------------
// Init sites, instrumented rather than reasoned about (the ScatterExt lesson:
// a starved hook and a hook that logged before the log file existed produce
// an identical empty log). Each site sets a flag; WeaponDiag::ReportOnce
// prints them later from a site already proven live (the bounty probe).
//
// Do NOT log from any of these. 0x7CD810 is the first instruction of WinMain,
// long before the command line is parsed and debug.log is opened.
// ---------------------------------------------------------------------------

// Contested: Ares/Antares, Phobos, Kratos, CnCNet-Spawner and AggressiveStance
// all hook this address. Syringe chains handlers, but the first to return a
// non-zero address wins and the remainder never execute -- so this may never
// run, which would mean Patch::ApplyStatic() never runs either.
DEFINE_HOOK(0x7CD810, WeaponExt_ExeRun, 0x9)
{
	WeaponDiag::MarkExeRun();
	ScatterDiag::MarkExeRun();
	WeaponExtDLL::ExeRun();
	return 0;
}

// Uncontested control. `mov eax, fs:0` -- 6 bytes, one whole instruction, no
// relative branch, so re-executing it from Syringe's trampoline is safe.
DEFINE_HOOK(0x7CD81E, WeaponExt_ExeRunAlt, 0x6)
{
	WeaponDiag::MarkExeRunAlt();
	ScatterDiag::MarkExeRunAlt();
	WeaponExtDLL::ExeRun();
	return 0;
}

// Contested (six frameworks). Marks only; ScatterExt's diagnostics proved all
// three init sites run fine in our chain slot, and this flag keeps that
// evidence alive in the merged DLL.
DEFINE_HOOK(0x52F639, WeaponExt_CmdLineParse, 0x5)
{
	ScatterDiag::MarkCmdLine();
	return 0;
}
