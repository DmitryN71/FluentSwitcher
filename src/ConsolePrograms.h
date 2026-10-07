// Консольные программы, в которых автопереключение всё же работает (autoswitch_console, Settings.h). В консоли его нет:
// пароль там вводят в приглашение (ssh, sudo, runas), и Windows не скажет, что это пароль; а в командной строке
// набирают команды. Но FAR - редактор и просмотрщик в окне консоли (Дмитрий 07.10, форум: "ни в какую не хочет
// работать в FAR"), и такие программы человек добавляет в список сам.
//
// Окно консоли разрешено, если его программа - из списка: имя exe или путь, строчными. В обычной консоли
// (ConsoleWindowClass) окно принадлежит первой программе в ней (cmd.exe или far.exe, запущенный ярлыком), поэтому
// смотрим и запущенные из неё по дереву процессов: far.exe из cmd. В Windows Terminal, ConEmu, mintty окно - их
// собственное, а вкладки по процессам не различить: там только сама программа окна (WindowsTerminal.exe - все вкладки).
// Ответ на окно помнится 2 с (у каждого потока свой: спрашивают и хук, и движок): снимок процессов - не на каждую клавишу.
#pragma once

#include <windows.h>
#include <tlhelp32.h>

#include <set>
#include <string>
#include <vector>

namespace ConsolePrograms {

inline std::wstring Lower(std::wstring s) {
	for (auto& c : s) c = (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)c);
	for (auto& c : s)
		if (c == L'/') c = L'\\';
	return s;
}

// Путь exe процесса строчными; не узнать - пусто.
inline std::wstring PathOf(DWORD pid) {
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process) return {};
	wchar_t path[MAX_PATH * 2] = {};
	DWORD size = MAX_PATH * 2;
	const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
	CloseHandle(process);
	return ok ? Lower(path) : std::wstring();
}

inline bool Allowed(HWND w, const std::set<std::wstring>& programs) {
	if (programs.empty() || !w) return false;
	thread_local struct {
		HWND w = nullptr;
		ULONGLONG at = 0;
		bool allowed = false;
	} cache;
	const ULONGLONG now = GetTickCount64();
	if (cache.w == w && now - cache.at < 2000) return cache.allowed;

	DWORD root = 0;
	GetWindowThreadProcessId(w, &root);
	wchar_t cls[64] = {};
	GetClassNameW(w, cls, 64);
	const bool tree = wcscmp(cls, L"ConsoleWindowClass") == 0;

	std::vector<PROCESSENTRY32W> all;
	if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); snap != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32W e{ sizeof(e) };
		for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) all.push_back(e);
		CloseHandle(snap);
	}
	std::set<DWORD> ours{ root };
	for (bool more = tree; more;) {
		more = false;
		for (const auto& p : all)
			if (p.th32ProcessID != p.th32ParentProcessID && ours.contains(p.th32ParentProcessID) &&
			    ours.insert(p.th32ProcessID).second)
				more = true;
	}
	bool allowed = false;
	for (const auto& p : all)
		if (ours.contains(p.th32ProcessID) && programs.contains(Lower(p.szExeFile))) allowed = true;
	bool paths = false; // в списке есть и пути, не только имена
	for (const auto& p : programs) paths = paths || p.find(L'\\') != std::wstring::npos;
	for (DWORD pid : ours)
		if (!allowed && paths) {
			const std::wstring path = PathOf(pid);
			allowed = !path.empty() && programs.contains(path);
		}
	cache = { w, now, allowed };
	return allowed;
}

}
