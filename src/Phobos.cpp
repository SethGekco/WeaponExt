#include <Phobos.h>

// Static member definitions required by Container.h and other Phobos utilities.
// Minimal stub -- we define only what this project needs.

HANDLE Phobos::hInstance = nullptr;

char Phobos::readBuffer[Phobos::readLength];
wchar_t Phobos::wideBuffer[Phobos::readLength];

// Needed by Phobos's FlyingStrings.cpp, which we compile in to render the
// bounty "+$25" money text. Phobos.INI.cpp defines this as L"" and fills it
// from UI.ini; we are not reading UI.ini, so the empty default stands and the
// string renders as a bare "+25".
const wchar_t* Phobos::UI::CostLabel = L"$";

// Stub implementations of Phobos lifecycle methods we don't use.
void Phobos::CmdLineParse(char**, int) { }
void Phobos::ExeRun() { }
void Phobos::ExeTerminate() { }
