#pragma once

// Окна настроек этого же exe (FluentSwitcher.exe --settings - свой процесс): их ищут --quit (WinMain.cpp) и "Выход" в
// трее (TrayIcon.h).
namespace SettingsWindows {
// Полный путь exe процесса pid (пусто - не узнать).
inline std::wstring ProcessPath(DWORD pid) {
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process) return {};
	wchar_t path[MAX_PATH * 2] = {};
	DWORD size = (DWORD)std::size(path);
	bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
	CloseHandle(process);
	return ok ? std::wstring(path, size) : std::wstring();
}

// Открытые окна настроек этого же exe (заголовок "FluentSwitcher") - закрыть (WM_CLOSE: окно запоминает место,
// несохранённое не пишет). pids - их процессы, чтобы дождаться. Выход из трея оставлял окно настроек открытым без
// движка (форум, Maz, 08.11.2026).
inline void CloseAll(std::vector<DWORD>* pids = nullptr) {
	std::wstring self;
	IFS_LOG(PathUtils::GetPath_exe_noLower(self));
	std::function<void(HWND)> close = [&](HWND window) {
		wchar_t title[64] = {};
		GetWindowTextW(window, title, (int)std::size(title));
		DWORD pid = 0;
		GetWindowThreadProcessId(window, &pid);
		if (wcscmp(title, L"FluentSwitcher") == 0 && IsWindowVisible(window) && pid != GetCurrentProcessId() &&
			StrUtils::IsEqualCI(ProcessPath(pid).c_str(), self.c_str())) {
			if (pids) pids->push_back(pid);
			PostMessageW(window, WM_CLOSE, 0, 0);
		}
	};
	EnumWindows([](HWND window, LPARAM param) -> BOOL {
		(*reinterpret_cast<std::function<void(HWND)>*>(param))(window);
		return TRUE;
	}, reinterpret_cast<LPARAM>(&close));
}
}
