#pragma once

#include "ConfigData.h"
#include "ConfigData_hk.h"
#include "RemoteDesktop.h"

enum class SeparateExtMode {
	Disabled = 3,
	Symbol = 0,
	PossibleSymb_SeveralW = 1,
	PossibleSymb_Always = 2,
};

class ProgramConfig {
	struct CHotKeySet {
		HotKeyType hkId = hk_NULL;
		CHotKeyList keys;
	};
public:
	static auto GetPath_Conf() { return PathUtils::GetPath_folder_noLower2() / L"FluentSwitcher.json"; }

    ProgramConfig() {

		auto add = [&](HotKeyType type, bool usedef = false) {
			hotkeysList.emplace_back(type);
			if (usedef)
				hotkeysList.back().keys.key() = *(GetHk_Defaults(type).begin());
			};

		add(hk_RevertLastWord,true);
		add(hk_RevertSeveralWords,true);
		add(hk_RevertAllRecentText,true);
		add(hk_RevertSelelected,true);
		add(hk_CycleSwitchLayout);
		add(hk_EmulateCapsLock);
		add(hk_toUpperSelected);
		add(hk_InvertCaseSelected);
		add(hk_ToggleEnabled,true);
		add(hk_ShowMainWindow,true);
		// hk_ShowRemainderWnd - "Напоминалка" SimpleSwitcher, в FluentSwitcher её нет.
		// hk_InsertWithoutFormat ("вставить без оформления") не нужен переключателю раскладки: это есть у
		// менеджера буфера (FluentClipper: Ctrl+Shift+Insert).
		add(hk_RevertLine);
    }

    std::set <std::wstring> disableInPrograms;

    void NormalizePaths() {
		std::set <std::wstring> res;
        for (const auto& it : disableInPrograms) {
			auto cur = it;
			StrUtils::ToLower(cur);
            PathUtils::NormalizeDelims(cur);
            res.insert(std::move(cur)); // todo cast
        }
		disableInPrograms = std::move(res);
    }
    // Программа впереди - из disableInPrograms или окно удалённого рабочего стола, виртуальной машины (RemoteDesktop.h).
    bool IsSkipProgramTop() const {

        const auto& col = disableInPrograms;

        auto info = Utils::GetFocusedWndInfo();

        std::wstring path;
        std::wstring name;
        IFS_LOG(Utils::GetProcLowerNameByPid(info.pid_top, path, name));
        if (name.empty()) {
            LOG_ANY(L"can't find name. pid={}", info.pid_top);
            return false;
        }

        if (RemoteDesktop::IsClient(name)) {
            LOG_ANY(L"Skip process {}: a remote desktop or a virtual machine", name);
            return true;
        }

        if (col.contains(name)) {
            LOG_ANY(L"Skip process by name {} because of disableInProcess", name);
            return true;
        }

        if (col.contains(path)) {
            LOG_ANY(L"Skip process by path {} because of disableInProcess", path);
            return true;
        }

        return false;
    }

    TLogLevel logLevel = LOG_LEVEL_3;

    string config_version;

    bool fixRAlt = false;
    HKL fixRAlt_lay_ = (HKL)0x4090409;
    bool isMonitorAdmin = false;
    bool force_DbgMode              = false;
    bool fClipboardClearFormat = false;
    bool disableAccessebility    = false;
	bool ShowLangsInTrayMenu = true;
	bool ShowReminderInTrayMenu = false;
	bool ShowRunProgramsInTrayMenu = false;
    static constexpr UStr showFlags_OriginalFlags = "Original Flags";
    static constexpr UStr showFlags_AppIcon = "Application Icon";
    static constexpr UStr showFlags_Nothing = "Nothing";
    string flagsSet = "Glossy";
    //bool SkipAllInjectKeys = false;
    bool SkipLowLevelInjectKeys = false; // с 1.5.0 не действует (HookerKeyboard.cpp): поле остаётся в файле настроек
    bool AlternativeLayoutChange = false;
	uint32_t quick_press_ms = 280;
	SeparateExtMode separate_ext_mode = SeparateExtMode::Symbol;
    CHotKey win_hotkey_cycle_lang { VK_LMENU, VK_SHIFT };
	std::string theme = "Light";
	string ui_skin = "";
	// Тема окна настроек: "" - как в Windows, "Light", "Dark" (theme выше - от окна SimpleSwitcher, не используется).
	string ui_theme = "";
	// Окно настроек: записывать левые и правые Ctrl, Shift, Alt, Win по отдельности.
	bool record_sides = false;
	// Раз в день спрашивать у GitHub номер последней версии (Update.h).
	bool check_updates = true;
	// Щелчки по флагу у часов: "" - ничего, "menu", "next_layout", "toggle", "settings" (TrayIcon.h).
	string tray_click = "";
	string tray_double_click = "settings";
	// Звуки (LayoutSound.h), громкость в процентах, 0 - без звука: переключение раскладки (сочетанием, щелчком по
	// флагу) и исправление текста.
	int sound_switch = 0;
	int sound_fix = 0;
	// ДВе ЗАглавные (TwoCaps.h): исправлять после пробела; свои исключения (UTF-8).
	bool two_caps = false;
	// Английское i отдельным словом - I (TwoCaps::LoneI; не в консоли и не в редакторах кода).
	bool fix_lone_i = true;
	std::vector<std::string> two_caps_exceptions;
	// Сколько раз слово возвращали сразу после исправления; на третий - в исключения.
	std::map<std::string, int> two_caps_undo;
	// Автопереключение раскладки (AutoSwitch.h): слово не в той раскладке исправляется само в конце слова; свои
	// исключения (UTF-8, в любой из двух форм) и счёт отмен (на третью - в исключения).
	bool autoswitch = false;
	// Не ждать конца слова: переключать с четвёртой буквы (AutoSwitch::DecideEarly), если так не начинается ни одно
	// слово своего языка, а в другой раскладке - начинается.
	bool autoswitch_early = true;
	std::vector<std::string> autoswitch_exceptions;
	std::map<std::string, int> autoswitch_undo;
	// Переключать всегда (в нужном виде): слова, которые словарь не знает или знает в другом языке ("еру" - the), и
	// одиночные буквы ("ф" - a). Слово попадает сюда и само: исправленное вручную ("Исправить последнее слово") в
	// третий раз - счёт в autoswitch_fix (в нужном виде, строчными).
	std::vector<std::string> autoswitch_force = { "the", "a" };
	std::map<std::string, int> autoswitch_fix;
	// Слова, которые приложение добавило в списки само (третья отмена, третье исправление вручную): по спискам -
	// "autoswitch_exceptions", "autoswitch_force", "two_caps_exceptions". Окно настроек отмечает их "выучено".
	std::map<std::string, std::vector<std::string>> learned;
	// Журнал автопереключения (log\autoswitch.log): что переключилось само, что вернули, что исправили вручную.
	bool autoswitch_journal = false;
	// Язык меню у флага (и окна настроек): "Russian", "English", "Ukrainian"; без настройки - как у Windows.
	string gui_lang = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN ? "Russian"
		: PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_UKRAINIAN ? "Ukrainian" : "English";
	bool useBritishFlag = false;
	string treat_as_letters = "_-";
	// Перепечатывать исправленное клавишами (как в SimpleSwitcher), а не готовыми символами.
	// Символы не зависят от Shift и от того, успела ли смениться раскладка: новый Блокнот
	// Windows 11 терял Shift в быстрой пачке клавиш ("?" -> "." вместо ",").
	bool retype_keys = false;
	// Пауза между символами при перепечатке и между Backspace'ами, мс. Пачку, отправленную разом, новый
	// Блокнот Windows 11 не выдерживает: теряет и повторяет символы ("ooooони тттт"). По одному с паузой
	// 8 мс он справляется (проверено в FluentClipper). 0 - всё разом, как раньше.
	uint32_t retype_delay_ms = 8;
	//int inject_delay_after_lang_ms = 0;
	// Флажок раскладки у текстового курсора (CaretFlag.h): 0 - нет, 1 - всегда, 2 - ненадолго после смены
	// раскладки или окна (caret_flag_brief_ms). Размер - в точках при 100 % (картинка набора флагов ближайшего
	// размера). Место: 0 - под кареткой, 1 - над ней. Непрозрачность - в процентах.
	int caret_flag = 1;
	int caret_flag_size = 20;
	int caret_flag_place = 0;
	int caret_flag_opacity = 60;
	int caret_flag_brief_ms = 2000;

    std::vector< CHotKeySet> hotkeysList;
    std::vector< RunProgramInfo> run_programs;

    LayoutInfoList layouts_info;

    const auto& GetHk(HotKeyType type) const {
        for (auto& it : hotkeysList) {
            if (it.hkId == type) return it;
        }
        LOG_ANY(L"CRITICAL ERR");
        std::terminate();
    }

    std::generator < std::tuple<HotKeyType, const CHotKey&>> All_hot_keys() const {
        for (const auto& it : hotkeysList) {
            for (const auto& key : it.keys.keys) {
                co_yield{ it.hkId, key };
            }

        }
        for (int i = -1; const auto& it : layouts_info.info) {
            i++;
            for (const auto& key : it.hotkey.keys) {
                co_yield{ (HotKeyType)(hk_SetLayout_flag | i), key };
            }
        }
        for (int i = -1; const auto & it : run_programs) {
            i++;
            for (const auto& key : it.hotkey.keys) {
                co_yield{ (HotKeyType)(hk_RunProgram_flag | i), key };
            }
        }
    }

};


namespace cfg_details {

	using ConfPtr = std::shared_ptr<ProgramConfig>;

	inline constinit ConfPtr g_config;
	inline constinit std::unique_ptr<ProgramConfig> g_guiCfg;

	inline auto conf_gui() { return g_guiCfg.get(); }

	TStatus LoadConfig(ProgramConfig& cfg);
	TStatus Save_conf(const ProgramConfig& gui);
	// В файле нет поля, которое движок пишет (появилось в новой версии с тем же номером): тогда файл
	// дописывается, чтобы новое поле было видно и его можно было править вручную или в окне настроек.
	bool FileMissesFields(const ProgramConfig& cfg);
	TStatus Save_conf_To_Stream(std::ostream& outp, const ProgramConfig& gui);

	inline void ApplyGuiConfig() {
		ConfPtr ptr = MAKE_SHARED(ptr);
		*ptr = *conf_gui();
		g_config.swap(ptr);
	}

	inline void SaveGuiConfig() {
		IFS_LOG(Save_conf(*conf_gui()));
	}

	inline bool ReloadGuiConfig() {
		bool res = true;
		if (!g_guiCfg) {
			g_guiCfg = MAKE_UNIQUE(g_guiCfg);
		}
		auto errLoadConf = LoadConfig(*conf_gui());
		if (errLoadConf != TStatus::SW_ERR_SUCCESS) {
			IFS_LOG(errLoadConf);
			res = false;
		}
		else {
			if (conf_gui()->config_version != GET_SW_VERSION()) {
				conf_gui()->config_version = GET_SW_VERSION();
				SaveGuiConfig();
			}
			else if (FileMissesFields(*conf_gui())) {
				SaveGuiConfig();
			}
		}
		ApplyGuiConfig();
		return res;
	}
}

/*
Многопоточный конфиг
	conf_gui() - read/write from gui thread
	conf_get_unsafe, GETCONF - read from any threads
*/

inline auto conf_get_unsafe() { // Проблемы синтаксиса conf_get_unsafe()->...  1) std::generator не держит temporary 2) множественном вызов даст другую версию.
    auto res = std::const_pointer_cast<const ProgramConfig>(cfg_details::g_config);
    return res; 
}

inline auto conf_gui() { return cfg_details::conf_gui();}

#define GETCONF auto cfg = conf_get_unsafe();


inline void SaveApplyGuiConfig() {
	cfg_details::ApplyGuiConfig();
	cfg_details::SaveGuiConfig();
	
}

// Список раскладок настроек (layouts_info) = раскладки Windows: убрать удалённые, добавить новые (включёнными).
// Без него исправлять не на что: новый файл настроек приходит с пустым списком. Раньше это делало старое окно
// ImGui при каждом запуске (gui2/gui_utils.h), с test22 его нет - теперь при запуске, после перечитывания
// настроек и когда в Windows появилась раскладка, которой нет в списке. Только в главном потоке (conf_gui).
inline void SyncLayouts() {
	HKL all_lays[50] = { 0 };
	int all_lay_size = GetKeyboardLayoutList((int)std::size(all_lays), all_lays);
	if (all_lay_size <= 0) return;
	auto has_system_layout = [&](HKL lay) { return std::find(all_lays, all_lays + all_lay_size, lay) != all_lays + all_lay_size; };

	auto& list = conf_gui()->layouts_info;
	auto& info = list.info;
	bool was_changes = false;
	for (int i = (int)info.size() - 1; i >= 0; i--) {
		if (!has_system_layout(info[i].layout)) {
			Utils::RemoveAt(info, i);
			was_changes = true;
		}
	}
	for (int i = 0; i < all_lay_size; i++) {
		if (!list.HasLayout(all_lays[i])) {
			info.push_back({ .layout = all_lays[i] });
			was_changes = true;
		}
	}
	if (was_changes) {
		LOG_ANY("layouts synced with Windows: {}", info.size());
		SaveApplyGuiConfig();
	}
}

inline void ApplyLocalization() {
	Localization::Reinit(conf_gui()->gui_lang.c_str());
}

inline void SetLogLevel_print_info(TLogLevel logLevel) {
	SetLogLevel(logLevel);

	LOG_ANY("Log level now {}. ver: {}", (int)logLevel, GET_SW_VERSION());
	LOG_ANY("Is Admin: {}", Utils::IsSelfElevated());
	LOG_ANY("IsWindows11OrGreater: {}", IsWindows11OrGreater());

	// Настройки ещё не загружены (force_DbgMode: журнал включается посреди их загрузки) - нечего показывать.
	if (GetLogLevel() >= LOG_LEVEL_2 && conf_get_unsafe()) {
		std::ostringstream buffer;
		cfg_details::Save_conf_To_Stream(buffer, *conf_get_unsafe());
		LOG_ANY("CONFIG:\n{}", buffer.str());
	}

}


