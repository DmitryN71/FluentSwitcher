// Окна удалённого рабочего стола и виртуальных машин (mstsc, Windows App, Hyper-V, VMware, VirtualBox, TeamViewer,
// AnyDesk, RustDesk, Parsec, VNC, менеджеры подключений): набранное там уходит в другой компьютер со своей раскладкой
// и, может быть, своим FluentSwitcher. Там FluentSwitcher этого компьютера не работает, как в программах из
// disableInPrograms (Settings.h): стёртое и перепечатанное отсюда приходит туда искажённым, раскладка этого компьютера
// там ничего не решает, а сочетание, съеденное здесь, не дошло бы до копии там (Дмитрий 06.10: Блокнот дома по RDP -
// "b xnj" на миг стало "и что", а потом пробелами: слово исправляли обе копии).
#pragma once

#include <windows.h>
#include <string>

namespace RemoteDesktop {

// Имя файла программы строчными: mstsc.exe.
inline bool IsClient(const std::wstring& name) {
	static const wchar_t* const clients[] = {
		L"mstsc.exe",                    // Подключение к удалённому рабочему столу
		L"msrdc.exe", L"msrdcw.exe",     // Windows App, клиент удалённого рабочего стола
		L"vmconnect.exe",                // Hyper-V
		L"vmware.exe", L"vmplayer.exe",  // VMware Workstation, Player
		L"virtualboxvm.exe",             // VirtualBox
		L"teamviewer.exe", L"anydesk.exe", L"rustdesk.exe", L"parsecd.exe",
		L"vncviewer.exe", L"tvnviewer.exe",
		L"rdcman.exe", L"mremoteng.exe", L"royalts.exe", L"remotedesktopmanager.exe",
	};
	for (const wchar_t* client : clients)
		if (name == client) return true;
	return false;
}

// Программа process - из них (process - с PROCESS_QUERY_LIMITED_INFORMATION).
inline bool IsClientProcess(HANDLE process) {
	wchar_t path[MAX_PATH];
	DWORD size = MAX_PATH;
	if (!process || !QueryFullProcessImageNameW(process, 0, path, &size)) return false;
	const wchar_t* slash = wcsrchr(path, L'\\');
	std::wstring name = slash ? slash + 1 : path;
	for (auto& c : name) c = towlower(c);
	return IsClient(name);
}

}
