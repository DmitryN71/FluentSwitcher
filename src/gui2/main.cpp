// Главный поток движка: служебное окно (таймеры, команды окна настроек - SettingsIpc.h), значок у часов
// и флажок у текстового курсора. Окно настроек - этот же exe с --settings, своим процессом (settings/).

#include "TrayIcon.h"
#include "CaretFlag.h"
#include "SettingsIpc.h"
#include "utils/WinTimer.h"

void StartGui() {

	// Служебное окно + таймеры
	WinTimer timer;
	g_guiHandle = timer.GetHandler();

	// Команды окна настроек (SettingsIpc.h).
	SettingsIpc::AllowFromNormalPrograms(g_guiHandle);
	timer.AnswerHandler([](UINT msg, WPARAM wParam, LPARAM) { return SettingsIpc::Handle(msg, wParam); });

	TrayIcon trayIcon;
	CaretFlag caretFlag;

	timer.CustomHandler(
		[&](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
			if (msg == WM_ShowWindow) {
				// 0 - окно настроек; 1 - "Напоминалка" SimpleSwitcher, её в FluentSwitcher нет.
				if (wParam == 0) {
					std::wstring exe;
					IFS_LOG(PathUtils::GetPath_exe_noLower(exe));
					AllowSetForegroundWindow(ASFW_ANY); // окну настроек можно выйти на передний план
					auto res = (INT_PTR)ShellExecuteW(nullptr, L"open", exe.c_str(), L"--settings",
						PathUtils::GetPath_folder_noLower2().c_str(), SW_SHOWNORMAL);
					if (res <= 32) {
						LOG_WARN(L"can't start {} --settings: {}", exe, (int)res);
					}
				}
				return 0;
			}

			if (msg == WM_LayNotif) {
				// Раскладку добавили в Windows, пока мы работаем.
				if (wParam && !conf_gui()->layouts_info.HasLayout((HKL)wParam)) {
					SyncLayouts();
				}
				trayIcon.Update((HKL)wParam);
				caretFlag.OnLayout((HKL)wParam);
				return 0;
			}

			return 1;
		});

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}
}
