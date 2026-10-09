#pragma once

#include "utils/WinTray.h"
#include "IconManager.h"

class TrayIcon {
	HKL curlay = 0;
	uint64_t last_lay_cnt = 0;
	WinTray tray;
	HICON app_icon = 0;

	// Щелчки по флагу (tray_click, tray_double_click в настройках).
	enum class Click { Nothing, Menu, NextLayout, Toggle, Settings };
	static Click ClickOf(const std::string& s) {
		if (s == "menu") return Click::Menu;
		if (s == "next_layout") return Click::NextLayout;
		if (s == "toggle") return Click::Toggle;
		if (s == "settings") return Click::Settings;
		return Click::Nothing;
	}
	// Сразу при щелчке, пока он даёт право выводить окна вперёд: меню - на наше окно; раскладка - назад в окно,
	// где печатали (щелчок сделал активной панель задач), чтобы и переключить там, и сразу печатать дальше.
	void PrepareClick(Click c) {
		if (c == Click::Menu) SetForegroundWindow(tray.GetHandler());
		if (c == Click::NextLayout && m_lastApp && IsWindow(m_lastApp)) SetForegroundWindow(m_lastApp);
	}
	void RunClick(Click c, const char* how) {
		LOG_ANY("tray: {} -> action {}", how, (int)c);
		switch (c) {
		case Click::Menu: tray.ShowMenu(); break;
		case Click::NextLayout: Worker()->PostMsg([](auto w) { w->SwitchToNextLayout(); }, 50); break; // окно успело выйти вперёд
		case Click::Toggle: try_toggle_enable(); break;
		case Click::Settings: show_main_wind(); break;
		default: break;
		}
	}

	// Последнее окно, в котором работали: не панель задач и не её всплывающие окна.
	inline static TrayIcon* s_inst = nullptr;
	HWINEVENTHOOK m_fgHook = nullptr;
	HWND m_lastApp = nullptr;
	static bool IsShellWindow(HWND w) {
		wchar_t cls[64] = {};
		GetClassNameW(w, cls, 64);
		for (const wchar_t* name : { L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"NotifyIconOverflowWindow",
			L"TopLevelWindowForOverflowXamlIsland", L"XamlExplorerHostIslandWindow", L"Windows.UI.Core.CoreWindow" }) {
			if (wcscmp(cls, name) == 0) return true;
		}
		return false;
	}
	static void CALLBACK OnForeground(HWINEVENTHOOK, DWORD, HWND w, LONG idObject, LONG, DWORD, DWORD) {
		if (s_inst && w && idObject == OBJID_WINDOW && !IsShellWindow(w)) s_inst->m_lastApp = w;
	}
	Vec_i2 GetSize() {

#ifdef SS_WIN_7_COMPAT
		return { 16,16 };
#else
		UINT dpi = GetDpiForSystem();
		// Получаем ширину и высоту малой иконки (для трея) с учетом DPI
		int iconWidth = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
		int iconHeight = GetSystemMetricsForDpi(SM_CYSMICON, dpi);
		return { iconWidth, iconHeight };
#endif

	}
public:
	WinTray& TrayHandler() {
		return tray;
	}
	TrayIcon() {
		app_icon = LoadIcon(GetModuleHandle(0), MAKEINTRESOURCE(101));
		Update();
		s_inst = this;
		if (HWND fg = GetForegroundWindow(); fg && !IsShellWindow(fg)) m_lastApp = fg;
		m_fgHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnForeground, 0, 0,
			WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
		// Назначены оба щелчка - одиночный ждёт, не будет ли второго (время двойного щелчка из настроек Windows).
		tray.OnLeftClick([this]() {
			const Click single = ClickOf(conf_get_unsafe()->tray_click);
			if (single == Click::Nothing) return;
			PrepareClick(single);
			if (ClickOf(conf_get_unsafe()->tray_double_click) != Click::Nothing)
				tray.StartTimer(GetDoubleClickTime(), [this, single] { RunClick(single, "click (no double one came)"); });
			else
				RunClick(single, "click");
		});
		tray.OnDouble([this]() {
			tray.StopTimer();
			const Click twice = ClickOf(conf_get_unsafe()->tray_double_click);
			PrepareClick(twice);
			RunClick(twice, "double click");
		});
		tray.OnCreateMenu([this]() {
			std::vector<WinTray::TrayItem> res;

			// "Напоминалки" SimpleSwitcher (ShowReminderInTrayMenu) в FluentSwitcher нет.
			// Раскладок в меню нет (ShowLangsInTrayMenu не используется): пока меню открыто, активно оно само,
			// и выбранная раскладка доставалась не тому окну - пункты ничего не делали.

			// menu
			// Значки - знаки шрифта Segoe Fluent Icons: шестерёнка, клавиатура, выход.
			res.push_back({ .name = LOC("Settings"), .callback = []() { show_main_wind(); }, .icon = 0xE713 });
			res.push_back({ .name = LOC("Enabled"), .callback = []() { try_toggle_enable(); }, .is_checkbox = true,
			                .edit_val = g_enabled.IsEnabled(), .icon = 0xE765, .state = []() { return g_enabled.IsEnabled(); } });
			// Автопереключение (форум, 09.10.2026, AlexPORTrb: "и в контекстное меню значка в трее"): значок - две стрелки.
			res.push_back({ .name = LOC("Auto switch"), .callback = []() { SendMessage(g_guiHandle, WM_ToggleAutoswitch, 0, 0); },
			                .is_checkbox = true, .edit_val = conf_get_unsafe()->autoswitch, .icon = 0xE8AB,
			                .state = []() { return conf_get_unsafe()->autoswitch; } });
			res.push_back({ .is_separator = true });
			res.push_back({ .name = LOC("Exit"), .callback = []() { PostQuitMessage(0); }, .icon = 0xF3B1 });
			return res;
			});
	}
	~TrayIcon() {
		if (m_fgHook) UnhookWinEvent(m_fgHook);
		s_inst = nullptr;
	}
	// Значок в трее скрыт (tray_icon; до 07.10.2026 - flagsSet "Nothing", Settings.h, NormalizeFlags).
	static bool Hidden() {
		auto cfg = conf_get_unsafe();
		return !cfg->tray_icon || cfg->flagsSet == ProgramConfig::showFlags_Nothing;
	}
	// Уведомление у значка, щелчок по нему - onClick. Значок скрыт - уведомления нет.
	bool Notify(const std::wstring& title, const std::wstring& text, std::function<void()> onClick) {
		if (Hidden())
			return false;
		tray.OnBalloonClick(std::move(onClick));
		return tray.ShowBalloon(title, text);
	}
	void Update(HKL lay = 0) {

		if (lay != 0) {
			curlay = lay;
		}
		if (Hidden()) {
			tray.DeleteIcon();
			return;
		}
		if (curlay == 0) {
			curlay = Utils::GetFocusedWndInfo().lay;
		}

		auto id  = Utils::GetNameForHKL_simple(curlay);
		LOG_ANY(L"mainguid new layout: {}, name={}", (void*)lay, id);

		if (!id.empty()) {
			auto icon = IconMgr::Inst().GetIcon(id.c_str(), GetSize(), !g_enabled.IsEnabled());
			if (icon->IsOk()) {
				tray.SetIcon(icon->hicon);
				return;
			}
		}

		// fallback
		tray.SetIcon(app_icon);
	}
};
