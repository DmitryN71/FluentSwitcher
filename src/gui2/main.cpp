// Главный поток движка: служебное окно (таймеры, команды окна настроек - SettingsIpc.h), значок у часов,
// флажок у текстового курсора и проверка обновлений (Update.h). Окно настроек - этот же exe с --settings, своим процессом (settings/).

#include "TrayIcon.h"
#include "CaretFlag.h"
#include "LayoutSound.h"
#include "SettingsIpc.h"
#include "Update.h"
#include "utils/WinTimer.h"

void WriteJournalLine(const std::string& utf8); // WorkerImplement.cpp: строка журнала автопереключения - в файл

void StartGui() {

	// Служебное окно + таймеры
	WinTimer timer;
	g_guiHandle = timer.GetHandler();

	// Команды окна настроек (SettingsIpc.h).
	SettingsIpc::AllowFromNormalPrograms(g_guiHandle);
	timer.AnswerHandler([](UINT msg, WPARAM wParam, LPARAM) { return SettingsIpc::Handle(msg, wParam); });

	TrayIcon trayIcon;
	CaretFlag caretFlag;
	LayoutSound layoutSound;

	// Счёт до трёх - отмены ДВух ЗАглавных и автопереключения, исправления вручную: на третий раз слово уходит в список
	// (исключения или "Переключать всегда"), о чём говорит уведомление у флага. true - ушло.
	auto countToList = [&](std::map<std::string, int>& counts, std::vector<std::string>& list, const std::wstring& word,
	                       const char* title, const char* text) {
		const std::string utf8 = StrUtils::Convert(word);
		if (++counts[utf8] < 3) return false;
		counts.erase(utf8);
		if (std::ranges::find(list, utf8) == list.end()) list.push_back(utf8);
		trayIcon.Notify(StrUtils::Convert(std::vformat(LOC(title), std::make_format_args(utf8))),
		                StrUtils::Convert(std::string(LOC(text))), [] { show_main_wind(); });
		return true;
	};
	// Счёт исправлений вручную изменился - в файл раз в минуту, а не на каждое исправление.
	bool fixCountsChanged = false;
	timer.CycleTimer([&] {
		if (!fixCountsChanged) return;
		fixCountsChanged = false;
		SaveApplyGuiConfig();
	}, 60 * 1000);

	// Буквы вместо флага - цвета текста панели задач: сменилась её тема (светлая / тёмная) - перерисовать значок.
	// Окна движка служебные (HWND_MESSAGE), WM_SETTINGCHANGE до них не доходит - смотрим раз в 2 с.
	timer.CycleTimer([&, dark = FluentMenu::TaskbarDark()]() mutable {
		const bool now = FluentMenu::TaskbarDark();
		if (now == dark) return;
		dark = now;
		if (LetterIcons::Is(conf_get_unsafe()->flagsSet)) trayIcon.Update();
	}, 2000);

	// Проверка обновлений: раз в минуту смотрим, не пора ли. Пора - через день после прошлого ответа GitHub
	// (первый раз - через минуту после запуска), после неудачи - не раньше чем через 6 часов. Запрос - в своём
	// потоке, ответ приходит сюда как WM_UpdateResult.
	const std::wstring folder = PathUtils::GetPath_folder_noLower2().wstring();
	struct {
		long long nextLook = 0; // раньше этого update.json даже не читаем
		long long lastTry = 0;
		bool busy = false;
	} updates;
	timer.CycleTimer([&] {
		if (!conf_get_unsafe()->check_updates || updates.busy) return;
		const long long now = Update::NowMs();
		if (now < updates.nextLook) return;
		updates.nextLook = now + 60LL * 60 * 1000; // не позже чем через час: файл могло обновить окно настроек
		const auto state = Update::Load(folder);
		if (now - state.checkedAt < Update::kDayMs) {
			updates.nextLook = std::min(updates.nextLook, state.checkedAt + Update::kDayMs);
			return;
		}
		if (now - updates.lastTry < 6LL * 60 * 60 * 1000) return;
		updates.lastTry = now;
		updates.busy = true;
		std::thread([hwnd = g_guiHandle] {
			auto* result = new Update::Result(Update::Check());
			if (!PostMessageW(hwnd, WM_UpdateResult, 0, (LPARAM)result)) delete result;
		}).detach();
	}, 60 * 1000);
	// Уведомление у флага о том, что нашла проверка. Само по себе - только о новой версии, один раз на версию
	// (щелчок открывает её страницу); после "Проверить сейчас" в окне настроек - всегда, с любым ответом, чтобы
	// было видно, что нажатие что-то сделало (как у FluentClipper).
	auto notifyUpdate = [&](Update::State& state, bool manual) {
		if (Update::NewerKnown(state)) {
			if (!manual && state.notified == state.latest) return;
			const auto title = StrUtils::Convert(std::vformat(LOC("FluentSwitcher {} is out"), std::make_format_args(state.latest)));
			const auto text = StrUtils::Convert(std::string(LOC("Click to open the download page")));
			const bool shown = trayIcon.Notify(title, text, [page = state.page] { OpenAsUser(page); });
			LOG_ANY("update: note about {} shown {}", state.latest, shown);
			if (shown)
				state.notified = state.latest;
		}
		else if (manual) {
			const std::string version = FS_VERSION;
			const bool shown = trayIcon.Notify(L"FluentSwitcher " + std::wstring(version.begin(), version.end()),
				StrUtils::Convert(std::string(state.failed ? LOC("Could not reach GitHub") : LOC("You have the latest version"))),
				nullptr);
			LOG_ANY("update: note after the check (failed {}) shown {}", state.failed, shown);
		}
	};

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

			if (msg == WM_UpdateResult) {
				std::unique_ptr<Update::Result> result(reinterpret_cast<Update::Result*>(lParam));
				updates.busy = false;
				if (!result->ok) {
					LOG_WARN("update check: {}", result->error);
					return 0;
				}
				LOG_ANY("update check: latest {}", result->latest);
				auto state = Update::Load(folder);
				Update::Apply(state, *result, false);
				notifyUpdate(state, false);
				Update::Save(folder, state);
				return 0;
			}

			if (msg == WM_TwoCapsLearn) {
				// Слово вернули сразу после исправления ДВух ЗАглавных. Счёт - в настройках (переживает перезапуск);
				// на третий раз слово уходит в исключения.
				std::unique_ptr<std::wstring> word(reinterpret_cast<std::wstring*>(lParam));
				LOG_ANY(L"two caps: {} brought back", *word);
				countToList(conf_gui()->two_caps_undo, conf_gui()->two_caps_exceptions, *word,
				            "\"{}\" will not be fixed any more",
				            "It is in the exceptions of TWo INitial CApitals: Settings, Typing");
				SaveApplyGuiConfig();
				return 0;
			}

			if (msg == WM_AutoSwitchLearn) {
				// Слово вернули сразу после автопереключения. Как у ДВух ЗАглавных: на третий раз - в исключения
				// автопереключения.
				std::unique_ptr<std::wstring> word(reinterpret_cast<std::wstring*>(lParam));
				LOG_ANY(L"autoswitch: {} switched back", *word);
				countToList(conf_gui()->autoswitch_undo, conf_gui()->autoswitch_exceptions, *word,
				            "\"{}\" will not be switched any more",
				            "It is in the exceptions of the layout auto switch: Settings, Auto switch");
				SaveApplyGuiConfig();
				return 0;
			}

			if (msg == WM_AutoSwitchLearnForce || msg == WM_AutoSwitchUnlearnForce) {
				// Слово исправили вручную ("Исправить последнее слово"), автопереключение его не тронуло: на третий раз - в
				// "Переключать всегда" (в нужном виде). (Maz на форуме, 07.10: "будет ли программа предлагать ... как
				// Пунто?") Исправили обратно сразу после - случайное нажатие: снять. Счёт меняется на каждое исправление:
				// в файл - не чаще раза в минуту (fixCountsChanged), сразу - только когда слово ушло в список; счёт "по разу"
				// при трёхстах словах в нём забывается - он не растёт без конца.
				std::unique_ptr<std::wstring> word(reinterpret_cast<std::wstring*>(lParam));
				auto& counts = conf_gui()->autoswitch_fix;
				if (msg == WM_AutoSwitchUnlearnForce) {
					const auto it = counts.find(StrUtils::Convert(*word));
					if (it != counts.end() && --it->second <= 0) counts.erase(it);
					LOG_ANY(L"autoswitch: {} fixed back by hand, not counted", *word);
					fixCountsChanged = true;
					return 0;
				}
				LOG_ANY(L"autoswitch: {} fixed by hand", *word);
				const bool learned = countToList(counts, conf_gui()->autoswitch_force, *word, "\"{}\" will always be switched",
				                                 "It is in \"Always switch\" of the layout auto switch: Settings, Auto switch");
				if (counts.size() > 300) std::erase_if(counts, [](const auto& c) { return c.second < 2; });
				if (learned) {
					fixCountsChanged = false;
					SaveApplyGuiConfig();
				}
				else
					fixCountsChanged = true;
				return 0;
			}

			if (msg == WM_JournalLine) {
				std::unique_ptr<std::string> line(reinterpret_cast<std::string*>(lParam));
				WriteJournalLine(*line);
				return 0;
			}

			if (msg == WM_AutoSwitchUnlearn) {
				// Отмену тут же исправили обратно ("Shift дважды" по привычке после автопереключения, потом ещё раз): переключение
				// было верным - отмена не в счёт.
				std::unique_ptr<std::wstring> word(reinterpret_cast<std::wstring*>(lParam));
				auto& counts = conf_gui()->autoswitch_undo;
				const auto it = counts.find(StrUtils::Convert(*word));
				if (it != counts.end()) {
					if (--it->second <= 0) counts.erase(it);
					LOG_ANY(L"autoswitch: {} fixed again after switching back, not counted", *word);
					SaveApplyGuiConfig();
				}
				return 0;
			}

			if (msg == WM_TextFixed) {
				layoutSound.OnFix();
				return 0;
			}

			if (msg == WM_UpdateChecked) {
				auto state = Update::Load(folder);
				notifyUpdate(state, true);
				Update::Save(folder, state);
				return 0;
			}

			if (msg == WM_LayNotif) {
				// Раскладку добавили в Windows, пока мы работаем.
				if (wParam && !conf_gui()->layouts_info.HasLayout((HKL)wParam)) {
					SyncLayouts();
				}
				trayIcon.Update((HKL)wParam);
				caretFlag.OnLayout((HKL)wParam);
				if (wParam)
					layoutSound.OnLayout((HKL)wParam, lParam != 0);
				else
					layoutSound.Reset(); // настройки перечитаны или FluentSwitcher включили / выключили
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
