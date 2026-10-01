
#include "TrayIcon.h"
#include "SwAutostart.h"



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
	if (OwnArgs().contains(L"--settings")) {
		return RunSettingsWindow(hInstance);
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
