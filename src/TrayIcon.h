#pragma once

#include "utils/WinTray.h"
#include "IconManager.h"

class TrayIcon {
	HKL curlay = 0;
	uint64_t last_lay_cnt = 0;
	WinTray tray;
	HICON app_icon = 0;
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
		tray.OnDouble([this]() { show_main_wind(); });
		tray.OnCreateMenu([this]() {
			std::vector<WinTray::TrayItem> res;

			// "Напоминалки" SimpleSwitcher (ShowReminderInTrayMenu) в FluentSwitcher нет.
			// Раскладок в меню нет (ShowLangsInTrayMenu не используется): пока меню открыто, активно оно само,
			// и выбранная раскладка доставалась не тому окну - пункты ничего не делали.

			// menu
			res.push_back({ .name = LOC("Settings"), .callback = []() { show_main_wind(); } });
			res.push_back({ .name = LOC("Enable"), .callback = []() { try_toggle_enable(); }, .is_checkbox = true, .edit_val = g_enabled.IsEnabled() });
			res.push_back({ .name = LOC("Exit"), .callback = []() { PostQuitMessage(0); } });
			return res;
			});
	}
	// Уведомление у флага, щелчок по нему - onClick. Флаг у часов скрыт ("Nothing") - уведомления нет.
	bool Notify(const std::wstring& title, const std::wstring& text, std::function<void()> onClick) {
		if (conf_get_unsafe()->flagsSet == ProgramConfig::showFlags_Nothing)
			return false;
		tray.OnBalloonClick(std::move(onClick));
		return tray.ShowBalloon(title, text);
	}
	void Update(HKL lay = 0) {

		if (lay != 0) {
			curlay = lay;
		}
		if (conf_get_unsafe()->flagsSet == ProgramConfig::showFlags_Nothing) {
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
