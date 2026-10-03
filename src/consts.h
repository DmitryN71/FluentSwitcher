#pragma once

// #define SW_INT_CHECK


static const UINT c_MSG_TypeHotKey = 0xBFFF - 37;
//static const UINT c_MSG_Quit = 0xBFFF - 35;
static const UINT WM_LayNotif = 0xBFFF - 29; 
static const UINT WM_ShowWindow = 0xBFFF - 30; 
static const UINT WM_ClearWordsBuffer = 0xBFFF - 28; 
static const UINT WM_UpdateResult = 0xBFFF - 27; // lParam - Update::Result* от потока проверки (gui2/main.cpp)

static const UINT c_timerKeyloggerDefence = 12;
static const TChar c_sArgAutostart[] = L"/autostart";




static const int c_nCommonWaitProcess = 5000;
static const int c_nCommonWaitMtx = 30000;

static const LPCWSTR c_wszTaskName = L"FluentSwitcherTask";
const static TChar c_sRegRunValue[] = L"FluentSwitcher";
// Автозапуск под прежним именем (SimpleSwitcher.exe): переносится на новое при запуске, SwAutostart.h.
static const LPCWSTR c_wszTaskNameOld = L"SimpleSwitcherTask";
const static TChar c_sRegRunValueOld[] = L"SimpleSwitcher";
const static TChar c_sExeNameOld[] = L"SimpleSwitcher.exe";

const inline ULONG_PTR c_MyInjectedId = (ULONG_PTR)(GetCurrentProcessId() ^ 0xACE1F345AABBCCDD);






