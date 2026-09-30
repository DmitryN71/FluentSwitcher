// Dear ImGui: standalone example application for Windows API + DirectX 11

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp


#include "imgui.h"

#include "backends_layer.h"

#include "main_wnd.h"
#include "TrayIcon.h"
#include "SettingsIpc.h"
#include "LoadFonts.h"
#include "utils/WinTimer.h"

// Main code
static void StartRealGui(MainWindow* wnd) {
	// Make process DPI aware and obtain main monitor scale

	if (!ImBackends::InitDisableMainViewport()) {
		LOG_ANY("error init");
		return;
	}

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigInputTextCursorBlink = false;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls


	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	SetStyle();

	// Setup scaling
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(ImBackends::main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = ImBackends::main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)
	io.ConfigDpiScaleFonts = true;          // [Experimental] Automatically overwrite style.FontScaleDpi in Begin() when Monitor DPI changes. This will scale fonts but _NOT_ scale sizes/padding for now.
	io.ConfigDpiScaleViewports = true;      // [Experimental] Scale Dear ImGui and Platform Windows when Monitor DPI changes.

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	// Setup Platform/Renderer backends
	ImBackends::InitRenders();

	LoadFonts();

	wnd->InitImGui();

	while (ImBackends::WaitNewFrame()) {

		ImBackends::NewFrame();

		// UI Logic
		wnd->DrawFrame();
		Notific::g_notif->Draw();

		// Timer update if needed
		if (Notific::g_notif->IsVisible()) {
			ImWantFrameWithDelay(0.5f);
		}

		ImBackends::RenderVSync();

#ifdef SS_WIN_7_COMPAT
		Sleep(1);
#endif
	}

	ImBackends::Cleanup();
}

void StartGui() {

	// Создаем главное окно + таймеры
	WinTimer timer;
	g_guiHandle = timer.GetHandler();

	// Команды отдельного окна настроек (SettingsIpc.h).
	SettingsIpc::AllowFromNormalPrograms(g_guiHandle);
	timer.AnswerHandler([](UINT msg, WPARAM wParam, LPARAM) { return SettingsIpc::Handle(msg, wParam); });

	// Создаем tray
	TrayIcon trayIcon;

	// Создаем нотификатор
	Notific::Notificator notif;
	Notific::g_notif = &notif;

	// Создаем главное окно
	MainWindow mainWindow(false);

	bool lightMode = true;

	timer.CycleTimer(
		[&]() {
			if (notif.Process()) {
				ImWantFrameWithDelay(0);
				lightMode = false;
			}
		}, 2000);

	timer.CustomHandler(
		[&](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
			if (msg == WM_ShowWindow) {
				int mode = wParam;
				// Новое окно настроек - отдельная программа рядом с движком; нет её - старое окно.
				if (mode == 0) {
					auto exe = PathUtils::GetPath_folder_noLower2() / L"FluentSwitcherSettings.exe";
					if (std::filesystem::is_regular_file(exe)) {
						AllowSetForegroundWindow(ASFW_ANY); // окну настроек можно выйти на передний план
						auto res = (INT_PTR)ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr,
							exe.parent_path().c_str(), SW_SHOWNORMAL);
						if (res > 32) {
							return 0;
						}
						LOG_WARN(L"can't start {}: {}", exe.wstring(), (int)res);
					}
				}
				if (mode) {
					notif.ShowHide();
				}
				else {
					mainWindow.ShowHide();
				}
				ImWantFrameWithDelay(0);
				lightMode = false;
				return 0;
			}

			if (msg == WM_LayNotif) {
				trayIcon.Update((HKL)wParam);
				return 0;
			}

			return 1;
		});

	while (lightMode) {
		MSG msg;
		if (GetMessage(&msg, NULL, 0, 0) <= 0) {
			return;
		}
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}

	StartRealGui(&mainWindow);

}

