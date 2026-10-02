
#include "TrayIcon.h"
#include "SwAutostart.h"
#include "SettingsIpc.h"

namespace {
// Полный путь exe процесса pid (пусто - не узнать).
std::wstring ProcessPath(DWORD pid) {
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process) return {};
	wchar_t path[MAX_PATH * 2] = {};
	DWORD size = (DWORD)std::size(path);
	bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
	CloseHandle(process);
	return ok ? std::wstring(path, size) : std::wstring();
}

// --quit (установщик перед заменой файла, удаление): закрыть FluentSwitcher из этой же папки - движок
// (командой Quit окна настроек, SettingsIpc.h) и открытые окна настроек - и дождаться, пока они выйдут.
// Копии из других папок не трогаем.
int QuitRunningCopy() {
	std::wstring self;
	IFS_LOG(PathUtils::GetPath_exe_noLower(self));
	std::vector<HANDLE> waits;
	auto ours = [&](DWORD pid) {
		return pid != GetCurrentProcessId() && StrUtils::IsEqualCI(ProcessPath(pid).c_str(), self.c_str());
	};
	auto wait_for = [&](DWORD pid) {
		if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid)) waits.push_back(h);
	};
	HWND engine = nullptr;
	while ((engine = FindWindowExW(HWND_MESSAGE, engine, L"SimpleSwitcher_Timer_001", nullptr)) != nullptr) {
		DWORD pid = 0;
		GetWindowThreadProcessId(engine, &pid);
		if (!ours(pid)) continue;
		wait_for(pid);
		DWORD_PTR res = 0;
		SendMessageTimeoutW(engine, SettingsIpc::msgQuit, 0, 0, SMTO_ABORTIFHUNG, 2000, &res);
	}
	// Окна настроек (FluentSwitcher.exe --settings): у них заголовок "FluentSwitcher".
	std::function<void(HWND)> close_settings = [&](HWND window) {
		wchar_t title[64] = {};
		GetWindowTextW(window, title, (int)std::size(title));
		DWORD pid = 0;
		GetWindowThreadProcessId(window, &pid);
		if (wcscmp(title, L"FluentSwitcher") == 0 && IsWindowVisible(window) && ours(pid)) {
			wait_for(pid);
			PostMessageW(window, WM_CLOSE, 0, 0);
		}
	};
	EnumWindows([](HWND window, LPARAM param) -> BOOL {
		(*reinterpret_cast<std::function<void(HWND)>*>(param))(window);
		return TRUE;
	}, reinterpret_cast<LPARAM>(&close_settings));
	for (HANDLE h : waits) {
		WaitForSingleObject(h, 5000);
		CloseHandle(h);
	}
	return 0;
}

// --cleanup (удаление программы): убрать автозапуск - значение Run и задачу планировщика, если они ведут
// к этому exe (копию в другой папке и её автозапуск не трогаем). Задачу с правами администратора без них
// не убрать: тогда эта же команда с правами (Windows спросит).
int CleanupAutostart() {
	COM::CAutoCOMInitialize com;
	IFS_LOG(com.Init());
	std::wstring exe;
	IFS_LOG(PathUtils::GetPath_exe_noLower(exe));
	bool runOurs = false;
	bool hasRun = false;
	IFS_LOG(CheckRegRun(runOurs, hasRun));
	if (runOurs) {
		IFS_LOG(Startup::RemoveWindowsRun(c_sRegRunValue));
	}
	Startup::CheckTaskSheduleParm task;
	task.taskName = c_wszTaskName;
	task.sPath = exe.c_str();
	task.sArgs = c_sArgAutostart;
	IFS_LOG(Startup::CheckTaskShedule(task));
	if (!task.isTaskExists || !task.isPathEqual) return 0;
	if (Utils::IsSelfElevated()) {
		IFS_LOG(Startup::RemoveTaskShedule(c_wszTaskName));
		return 0;
	}
	SHELLEXECUTEINFOW sei = { sizeof(sei) };
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;
	sei.lpVerb = L"runas";
	sei.lpFile = exe.c_str();
	sei.lpParameters = L"--cleanup";
	sei.nShow = SW_HIDE;
	if (ShellExecuteExW(&sei) && sei.hProcess) {
		WaitForSingleObject(sei.hProcess, 30000);
		CloseHandle(sei.hProcess);
	}
	return 0;
}
}

inline TStatus update_cur_dir() {
	std::wstring dir;
	IFS_RET(PathUtils::GetPath_folder_noLower(dir));
	IFW_RET(SetCurrentDirectory(dir.c_str()));
	RETURN_SUCCESS;
}


extern void StartGui();
extern int RunSettingsWindow(HINSTANCE instance); // settings/src/main.cpp
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

	// Окно настроек - этот же exe с --settings, своим процессом: движок тогда не запускается вовсе.
	// --quit и --cleanup - для установщика (installer/FluentSwitcher.iss).
	const auto args = OwnArgs();
	if (args.contains(L"--settings")) {
		return RunSettingsWindow(hInstance);
	}
	if (args.contains(L"--quit")) {
		return QuitRunningCopy();
	}
	if (args.contains(L"--cleanup")) {
		return CleanupAutostart();
	}

	setlocale(LC_ALL, "en_US.utf8");

	SetLogLevel(Utils::IsDebug() ? LOG_LEVEL_2 : LOG_LEVEL_DISABLE);

	// Настройки раньше назывались SimpleSwitcher.json: переименовываем, пока FluentSwitcher.json нет.
	{
		const auto dir = PathUtils::GetPath_folder_noLower2();
		std::error_code ec;
		if (!std::filesystem::exists(dir / L"FluentSwitcher.json", ec) && std::filesystem::exists(dir / L"SimpleSwitcher.json", ec)) {
			MoveFileExW((dir / L"SimpleSwitcher.json").c_str(), (dir / L"FluentSwitcher.json").c_str(), MOVEFILE_WRITE_THROUGH);
		}
	}

	if (!cfg_details::ReloadGuiConfig()) {
		MessageBox(
			NULL,
			L"Bad config",
			L"Error",
			MB_OK | MB_ICONERROR | MB_TASKMODAL
		);
		return 1;
	}

	IFS_LOG(update_cur_dir());
	SyncLayouts();
	LOG_ANY("Start program {}", GET_SW_VERSION());

	COM::CAutoCOMInitialize autoCom;
	IFS_LOG(autoCom.Init());

	if (conf_get_unsafe()->isMonitorAdmin && !Utils::IsSelfElevated() && RunElevatedCopy()) {
		LOG_ANY("an elevated copy took over");
		return 0;
	}

	MigrateOldAutostart();
	ApplyAutostartArg();

	CMainWorker::Inst().Init();

	ApplyLocalization();
	ApplyAcessebil();

	if (IsAdminOk()) {
		if (Utils::IsDebug() && !g_enabled.TryEnable()) {
			auto hk = conf_get_unsafe()->GetHk(hk_ToggleEnabled).keys.key();
			for (auto& it : hk) if (it == VKE_WIN) it = VK_LWIN;
			InputSender::SendHotKey(hk);
			Sleep(50);
		}
		g_enabled.TryEnable();
	}

	CoreWorker core;

	StartGui();

	LOG_ANY("program exit");

	return 0;

}
