// Консольные программы, в которых автопереключение всё же работает (autoswitch_console, Settings.h). В консоли его нет:
// пароль там вводят в приглашение (ssh, sudo, runas), и Windows не скажет, что это пароль; а в командной строке
// набирают команды. Но FAR - редактор и просмотрщик в окне консоли (Дмитрий 07.10, форум: "ни в какую не хочет
// работать в FAR"), и такие программы человек добавляет в список сам.
//
// Окно консоли разрешено, если его программа - из списка: имя exe или путь, строчными. В обычной консоли
// (ConsoleWindowClass) окно принадлежит первой программе в ней (cmd.exe или far.exe, запущенный ярлыком), поэтому
// смотрим и запущенные из неё по дереву процессов: far.exe из cmd. Но если из неё же запущена программа, которая
// спрашивает пароль (ssh, sudo, runas - PasswordPrompt), и её самой нет в списке - нельзя: FAR в списке не значит, что
// можно переключать пароль, набранный в ssh из FAR. В Windows Terminal, ConEmu, mintty окно - их собственное, а вкладки
// по процессам не различить: там только сама программа окна (WindowsTerminal.exe - все вкладки).
// Ответ на окно помнится 2 с (у каждого потока свой: спрашивают и хук, и движок): снимок процессов - не на каждую клавишу.
#pragma once

#include <windows.h>
#include <tlhelp32.h>

#include <map>
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

// Программы, которые спрашивают пароль в консоли.
inline bool PasswordPrompt(const std::wstring& lowerName) {
	for (const wchar_t* p : { L"ssh.exe", L"scp.exe", L"sftp.exe", L"plink.exe", L"pscp.exe", L"psftp.exe", L"telnet.exe",
	                          L"ftp.exe", L"sudo.exe", L"runas.exe", L"wsl.exe", L"bash.exe", L"mysql.exe", L"psql.exe" })
		if (lowerName == p) return true;
	return false;
}

// Когда процесс запущен; 0 - не узнать.
inline ULONGLONG CreatedAt(DWORD pid) {
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process) return 0;
	FILETIME created{}, exited{}, kernel{}, user{};
	const bool ok = GetProcessTimes(process, &created, &exited, &kernel, &user) != FALSE;
	CloseHandle(process);
	return ok ? ((ULONGLONG)created.dwHighDateTime << 32 | created.dwLowDateTime) : 0;
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
	bool paths = false; // в списке есть и пути, не только имена
	for (const auto& p : programs) paths = paths || p.find(L'\\') != std::wstring::npos;
	auto listed = [&](DWORD pid, const std::wstring& lowerName) {
		if (programs.contains(lowerName)) return true;
		if (!paths) return false;
		const std::wstring path = PathOf(pid);
		return !path.empty() && programs.contains(path);
	};

	if (!tree) { // Windows Terminal, ConEmu, mintty: только программа окна - снимок процессов не нужен
		const std::wstring path = PathOf(root);
		const std::wstring name = path.substr(path.find_last_of(L'\\') + 1);
		const bool allowed = !path.empty() && (programs.contains(name) || programs.contains(path));
		cache = { w, now, allowed };
		return allowed;
	}

	std::vector<PROCESSENTRY32W> all;
	if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); snap != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32W e{ sizeof(e) };
		for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) all.push_back(e);
		CloseHandle(snap);
	}
	// Запущенные из программы окна, по дереву. Номер родителя мог достаться новому процессу (родитель давно закрыт):
	// потомок старше такого "родителя" - не его.
	std::set<DWORD> ours{ root };
	std::map<DWORD, ULONGLONG> created;
	auto createdAt = [&](DWORD pid) {
		const auto it = created.find(pid);
		return it != created.end() ? it->second : (created[pid] = CreatedAt(pid));
	};
	for (bool more = true; more;) {
		more = false;
		for (const auto& p : all) {
			if (p.th32ProcessID == p.th32ParentProcessID || !ours.contains(p.th32ParentProcessID) ||
			    ours.contains(p.th32ProcessID))
				continue;
			const ULONGLONG parent = createdAt(p.th32ParentProcessID), child = createdAt(p.th32ProcessID);
			if (parent && child && child < parent) continue;
			ours.insert(p.th32ProcessID);
			more = true;
		}
	}
	bool allowed = false, prompt = false;
	for (const auto& p : all) {
		if (!ours.contains(p.th32ProcessID)) continue;
		const std::wstring name = Lower(p.szExeFile);
		if (listed(p.th32ProcessID, name))
			allowed = true;
		else if (PasswordPrompt(name))
			prompt = true;
	}
	allowed = allowed && !prompt;
	cache = { w, now, allowed };
	return allowed;
}

}
