#pragma once

#include "SwAutostart.h"

// Команды отдельного окна настроек (FluentSwitcher Settings) движку.
// Окно настроек находит окно движка FindWindowEx(HWND_MESSAGE, nullptr, L"SimpleSwitcher_Timer_001", nullptr)
// и шлёт ему SendMessageTimeout с сообщением RegisterWindowMessage(имя). Отвечает поток интерфейса движка.
// Сами настройки окно пишет в FluentSwitcher.json, движок только перечитывает файл.
//
//   SimpleSwitcher.ReloadConfig          -> 1; перечитать FluentSwitcher.json и применить
//   SimpleSwitcher.GetState              -> биты SettingsState
//   SimpleSwitcher.SetEnabled (0/1)      -> 1 или 0, если для включения нужны права администратора
//   SimpleSwitcher.SetAutostart (0/1)    -> 1 или 0, если не вышло (задаче планировщика нужны права администратора)
//   SimpleSwitcher.Quit                  -> 1; движок закрывается, как по "Выход" в меню трея
//   SimpleSwitcher.RunCommand (номер)    -> 1; выполнить команду run_programs[номер], как по её сочетанию
//   SimpleSwitcher.SetLogging (0/1/2)    -> 1; журнал отладки (log\FluentSwitcher.exe.log) до выхода движка; 2 - запись
//                                           для отчёта разработчику (безопасный журнал, Logger.h: log\FluentSwitcher-
//                                           report.log, без набранного текста, сама выключается через час); 0 - выключить,
//                                           ответ - когда файл записан
//   SimpleSwitcher.UpdateChecked         -> 1; окно проверило обновления (update.json): уведомление у флага с ответом
//
// Движок, запущенный от администратора, пропускает эти сообщения из обычной программы (ChangeWindowMessageFilterEx).

namespace SettingsIpc {

	enum SettingsState : LRESULT {
		State_Answered = 0x100, // есть всегда: 0 значит "движок не ответил"
		State_Enabled = 0x1,
		State_Elevated = 0x2,   // движок запущен от администратора
		State_Autostart = 0x4,
		State_Logging = 0x8,    // журнал отладки включён
		State_Report = 0x10,    // идёт запись для отчёта (безопасный журнал)
	};

	inline ULONGLONG reportSince = 0; // запись для отчёта включили (выключается сама через kReportMs - gui2/main.cpp)
	inline constexpr ULONGLONG kReportMs = 60 * 60 * 1000;

	// Начало записи для отчёта: что за программа и система, раскладки и сочетания - в её первые строки (сами настройки и
	// сведения о системе добавляет окно настроек, когда собирает отчёт).
	inline void ReportHeader() {
		auto cfg = conf_get_unsafe();
		LOG_ANY("report: FluentSwitcher {}, Windows 11 or later {}, elevated {}, can elevate {}, admin mode {}, enabled {}",
			GET_SW_VERSION(), IsWindows11OrGreater(), Utils::IsSelfElevated(), CanElevateSelf(), cfg->isMonitorAdmin,
			g_enabled.IsEnabled());
		LOG_ANY("report: autoswitch {}, early {}, two caps {}, caret flag {}, work in remote {}", cfg->autoswitch,
			cfg->autoswitch_early, cfg->two_caps, cfg->caret_flag, cfg->work_in_remote);
		for (const auto& l : cfg->layouts_info.info) {
			LOG_ANY(L"report: layout {:x} {} enabled {} Windows hotkey {}", (ULONGLONG)l.layout,
				LogPlain(Utils::GetNameForHKL_simple(l.layout)), l.enabled, LogPlain(StrUtils::Convert(LogHotKey(l.win_hotkey).s)));
			for (const auto& key : l.hotkey.keys)
				if (!key.IsEmpty())
					LOG_ANY("report: layout {:x} hotkey {}", (ULONGLONG)l.layout, LogHotKey(key));
		}
		for (const auto& hk : cfg->hotkeysList)
			for (const auto& key : hk.keys.keys)
				if (!key.IsEmpty())
					LOG_ANY("report: hotkey {} = {}", LogPlain(HotKeyTypeName(hk.hkId)), LogHotKey(key));
		LOG_ANY("report: Windows layout hotkey for the emulation {}", LogHotKey(cfg->win_hotkey_cycle_lang));
	}

	inline const UINT msgReloadConfig = RegisterWindowMessageW(L"SimpleSwitcher.ReloadConfig");
	inline const UINT msgGetState = RegisterWindowMessageW(L"SimpleSwitcher.GetState");
	inline const UINT msgSetEnabled = RegisterWindowMessageW(L"SimpleSwitcher.SetEnabled");
	inline const UINT msgSetAutostart = RegisterWindowMessageW(L"SimpleSwitcher.SetAutostart");
	inline const UINT msgQuit = RegisterWindowMessageW(L"SimpleSwitcher.Quit");
	inline const UINT msgRunCommand = RegisterWindowMessageW(L"SimpleSwitcher.RunCommand");
	inline const UINT msgSetLogging = RegisterWindowMessageW(L"SimpleSwitcher.SetLogging");
	inline const UINT msgUpdateChecked = RegisterWindowMessageW(L"SimpleSwitcher.UpdateChecked");

	inline void AllowFromNormalPrograms(HWND hwnd) {
		for (UINT msg : { msgReloadConfig, msgGetState, msgSetEnabled, msgSetAutostart, msgQuit, msgRunCommand, msgSetLogging, msgUpdateChecked }) {
			IFW_LOG(ChangeWindowMessageFilterEx(hwnd, msg, MSGFLT_ALLOW, nullptr));
		}
	}

	inline LRESULT State() {
		LRESULT res = State_Answered;
		if (g_enabled.IsEnabled()) res |= State_Enabled;
		if (Utils::IsSelfElevated()) res |= State_Elevated;
		if (autostart_get()) res |= State_Autostart;
		if (GetLogLevel() > LOG_LEVEL_DISABLE && !LogSafe()) res |= State_Logging;
		if (LogSafe()) res |= State_Report;
		return res;
	}

	// На потоке интерфейса движка (окно SimpleSwitcher_Timer_001).
	inline std::optional<LRESULT> Handle(UINT msg, WPARAM wParam) {
		if (msg == msgGetState) {
			return State();
		}
		if (msg == msgReloadConfig) {
			LOG_ANY("ipc: reload config");
			// Счёт исправлений вручную в памяти новее файла (в файл он идёт раз в минуту), а окно его не меняет: оставить
			// свой целиком - и снятое с него (исправили обратно, забытое при трёхстах словах) не вернётся из файла.
			auto fixCounts = std::move(cfg_details::conf_gui()->autoswitch_fix);
			cfg_details::ReloadGuiConfig();
			cfg_details::conf_gui()->autoswitch_fix = std::move(fixCounts);
			SyncLayouts(); // в файле могли остаться не все раскладки Windows
			ApplyAcessebil();
			ApplyLocalization(); // меню у значка и уведомления - на новом языке сразу (форум, gutasiho, 10.10.2026)
			if (!IsAdminOk()) {
				g_enabled.TryEnable(false); // "работать в программах от администратора" без прав - как в старом окне
			}
			new_layout_request(); // флаг в трее: набор флагов мог смениться
			return 1;
		}
		if (msg == msgSetEnabled) {
			bool on = wParam != 0;
			LOG_ANY("ipc: set enabled {}", on);
			if (on && !IsAdminOk()) {
				return 0;
			}
			g_enableTouched = true;
			if (g_enabled.TryEnable(on)) {
				new_layout_request();
				enabled_changed();
			}
			return g_enabled.IsEnabled() == on ? 1 : 0;
		}
		if (msg == msgSetAutostart) {
			LOG_ANY("ipc: set autostart {}", wParam != 0);
			return autostart_set(wParam != 0) ? 1 : 0;
		}
		if (msg == msgRunCommand) {
			LOG_ANY("ipc: run command {}", (int)wParam);
			auto hk = (HotKeyType)(hk_RunProgram_flag | (int)wParam);
			Worker()->PostMsg([hk](auto p) { p->RunProcess(hk); });
			return 1;
		}
		if (msg == msgSetLogging) {
			if (LogSafe()) SetLogSafe(false); // запись для отчёта - выключить и дописать до любого другого режима
			if (wParam == 2) {
				SetLogSafe(true);
				reportSince = GetTickCount64();
				ReportHeader();
			}
			else
				SetLogLevel_print_info(wParam ? conf_get_unsafe()->logLevel : LOG_LEVEL_DISABLE);
			return 1;
		}
		if (msg == msgUpdateChecked) {
			PostMessageW(g_guiHandle, WM_UpdateChecked, 0, 0); // уведомление показывает значок (gui2/main.cpp)
			return 1;
		}
		if (msg == msgQuit) {
			LOG_ANY("ipc: quit");
			PostQuitMessage(0);
			return 1;
		}
		return std::nullopt;
	}
}
