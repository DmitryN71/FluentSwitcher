// Окна удалённого рабочего стола и виртуальных машин (mstsc, Windows App, Hyper-V, VMware, VirtualBox, Citrix, Horizon,
// TeamViewer, AnyDesk, RustDesk, Parsec, NoMachine, Splashtop, VNC, менеджеры подключений): набранное там уходит в
// другой компьютер со своей раскладкой и, может быть, своим FluentSwitcher. Там FluentSwitcher этого компьютера
// работает, как везде, если не выключено "Работать в окнах удалённого доступа" (work_in_remote, Settings.h); выключено -
// молчит, как в программах из disableInPrograms: у кого FluentSwitcher и там, слово исправляли бы обе копии (Дмитрий
// 06.10: Блокнот дома по RDP - "b xnj" на миг стало "и что", а потом пробелами). Нажатия там не придерживаются
// всегда (KeyHold::CanHold), флага у курсора нет (CaretFlag: курсор там не этого компьютера).
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
		L"wfica32.exe", L"cdviewer.exe",           // Citrix Workspace
		L"vmware-view.exe", L"vmware-remotemks.exe", // VMware Horizon
		L"teamviewer.exe", L"anydesk.exe", L"rustdesk.exe", L"parsecd.exe", L"nxplayer.exe", L"strwinclt.exe",
		L"anyviewer.exe", L"aeroadmin.exe", L"supremo.exe",
		L"vncviewer.exe", L"tvnviewer.exe",
		L"rdcman.exe", L"mremoteng.exe", L"royalts.exe", L"remotedesktopmanager.exe",
	};
	for (const wchar_t* client : clients)
		if (name == client) return true;
	return false;
}

// Программа process - из них (process - с PROCESS_QUERY_LIMITED_INFORMATION).
inline bool IsClientProcess(HANDLE process) {
	wchar_t path[0x1000]; // пути длиннее MAX_PATH бывают: имя - в конце
	DWORD size = (DWORD)std::size(path);
	if (!process || !QueryFullProcessImageNameW(process, 0, path, &size)) return false;
	const wchar_t* slash = wcsrchr(path, L'\\');
	std::wstring name = slash ? slash + 1 : path;
	for (auto& c : name) c = towlower(c);
	return IsClient(name);
}

}
