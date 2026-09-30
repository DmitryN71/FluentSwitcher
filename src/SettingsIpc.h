#pragma once

// Команды отдельного окна настроек (FluentSwitcher Settings) движку.
// Окно настроек находит окно движка FindWindowEx(HWND_MESSAGE, nullptr, L"SimpleSwitcher_Timer_001", nullptr)
// и шлёт ему SendMessageTimeout с сообщением RegisterWindowMessage(имя). Отвечает поток интерфейса движка.
// Сами настройки окно пишет в SimpleSwitcher.json, движок только перечитывает файл.
//
//   SimpleSwitcher.ReloadConfig          -> 1; перечитать SimpleSwitcher.json и применить
//   SimpleSwitcher.GetState              -> биты SettingsState
//   SimpleSwitcher.SetEnabled (0/1)      -> 1 или 0, если для включения нужны права администратора
//   SimpleSwitcher.SetAutostart (0/1)    -> 1 или 0, если не вышло (задаче планировщика нужны права администратора)
//   SimpleSwitcher.Quit                  -> 1; движок закрывается, как по "Выход" в меню трея
//   SimpleSwitcher.RunCommand (номер)    -> 1; выполнить команду run_programs[номер], как по её сочетанию
//   SimpleSwitcher.SetLogging (0/1)      -> 1; журнал отладки (log\SimpleSwitcher.exe.log) до выхода движка
//
// Движок, запущенный от администратора, пропускает эти сообщения из обычной программы (ChangeWindowMessageFilterEx).

namespace SettingsIpc {

	enum SettingsState : LRESULT {
		State_Answered = 0x100, // есть всегда: 0 значит "движок не ответил"
		State_Enabled = 0x1,
		State_Elevated = 0x2,   // движок запущен от администратора
		State_Autostart = 0x4,
		State_Logging = 0x8,    // журнал отладки включён
	};

	inline const UINT msgReloadConfig = RegisterWindowMessageW(L"SimpleSwitcher.ReloadConfig");
	inline const UINT msgGetState = RegisterWindowMessageW(L"SimpleSwitcher.GetState");
	inline const UINT msgSetEnabled = RegisterWindowMessageW(L"SimpleSwitcher.SetEnabled");
	inline const UINT msgSetAutostart = RegisterWindowMessageW(L"SimpleSwitcher.SetAutostart");
	inline const UINT msgQuit = RegisterWindowMessageW(L"SimpleSwitcher.Quit");
	inline const UINT msgRunCommand = RegisterWindowMessageW(L"SimpleSwitcher.RunCommand");
	inline const UINT msgSetLogging = RegisterWindowMessageW(L"SimpleSwitcher.SetLogging");

	inline void AllowFromNormalPrograms(HWND hwnd) {
		for (UINT msg : { msgReloadConfig, msgGetState, msgSetEnabled, msgSetAutostart, msgQuit, msgRunCommand, msgSetLogging }) {
			IFW_LOG(ChangeWindowMessageFilterEx(hwnd, msg, MSGFLT_ALLOW, nullptr));
		}
	}

	inline LRESULT State() {
		LRESULT res = State_Answered;
		if (g_enabled.IsEnabled()) res |= State_Enabled;
		if (Utils::IsSelfElevated()) res |= State_Elevated;
		if (autostart_get()) res |= State_Autostart;
		if (GetLogLevel() > LOG_LEVEL_DISABLE) res |= State_Logging;
		return res;
	}

	// На потоке интерфейса движка (окно SimpleSwitcher_Timer_001).
	inline std::optional<LRESULT> Handle(UINT msg, WPARAM wParam) {
		if (msg == msgGetState) {
			return State();
		}
		if (msg == msgReloadConfig) {
			LOG_ANY("ipc: reload config");
			cfg_details::ReloadGuiConfig();
			ApplyAcessebil();
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
			if (g_enabled.TryEnable(on)) {
				new_layout_request();
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
			SetLogLevel_print_info(wParam ? conf_get_unsafe()->logLevel : LOG_LEVEL_DISABLE);
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
