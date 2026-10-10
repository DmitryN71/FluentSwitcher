// Главный поток движка: служебное окно (таймеры, команды окна настроек - SettingsIpc.h), значок у часов,
// флажок у текстового курсора и проверка обновлений (Update.h). Окно настроек - этот же exe с --settings, своим процессом (settings/).

#include "TrayIcon.h"
#include "CaretFlag.h"
#include "LayoutSound.h"
#include "SettingsIpc.h"
#include "Update.h"
#include "ConfigLock.h"
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
	// name - имя списка в настройках: слово отмечается выученным (learned), окно настроек так его и показывает.
	struct Learned {
		std::string word;
		ULONGLONG at = 0;
	};
	std::map<std::string, Learned> lastLearned; // по имени списка: слово, которое только что ушло туда (uncount)
	auto countToList = [&](std::map<std::string, int>& counts, std::vector<std::string>& list, const char* name,
	                       const std::wstring& word, const char* title, const char* text) {
		const std::string utf8 = StrUtils::Convert(word);
		if (++counts[utf8] < 3) return false;
		counts.erase(utf8);
		lastLearned[name] = { utf8, GetTickCount64() };
		if (std::ranges::find(list, utf8) == list.end()) list.push_back(utf8);
		auto& learned = conf_gui()->learned[name];
		if (std::ranges::find(learned, utf8) == learned.end()) learned.push_back(utf8);
		trayIcon.Notify(StrUtils::Convert(std::vformat(LOC(title), std::make_format_args(utf8))),
		                StrUtils::Convert(std::string(LOC(text))), [] { show_main_wind(); });
		return true;
	};
	// Отмену (исправление) тут же взяли назад - она не в счёт. Если она была третьей и слово уже ушло в список - вернуть
	// его оттуда, со счётом два. 0 - нечего снимать, 1 - снят счёт, 2 - слово убрано из списка.
	auto uncount = [&](std::map<std::string, int>& counts, std::vector<std::string>& list, const char* name,
	                   const std::wstring& word) {
		const std::string utf8 = StrUtils::Convert(word);
		if (const auto it = counts.find(utf8); it != counts.end()) {
			if (--it->second <= 0) counts.erase(it);
			return 1;
		}
		Learned& last = lastLearned[name];
		if (last.word != utf8 || GetTickCount64() - last.at > 60000) return 0;
		last = {};
		std::erase(list, utf8);
		std::erase(conf_gui()->learned[name], utf8);
		counts[utf8] = 2;
		LOG_ANY("{}: {} taken back from the list", name, utf8);
		return 2;
	};
	// Выученное в файл - как он есть на диске, со своим: там могут быть новые настройки из окна, которых мы ещё не
	// перечитали (окно сохранило их, пока мы ждали очереди к файлу, ConfigLock, - его ReloadConfig придёт следом), и
	// запись всего из памяти их бы стёрла. change - что поменять в прочитанном с диска. Файла нет - всё из памяти.
	// Прочитать не вышло (занят, испорчен): whole - всё из памяти, как раньше (выученное слово не терять); иначе -
	// ещё раз через минуту (счёт исправлений).
	bool fixCountsChanged = false;
	auto saveMerged = [&](const std::function<void(ProgramConfig&)>& change, bool whole) {
		ConfigLock lock;
		std::error_code ec;
		const bool exists = std::filesystem::is_regular_file(ProgramConfig::GetPath_Conf(), ec);
		ProgramConfig disk;
		const bool read = exists && cfg_details::LoadConfig(disk, false) == TStatus::SW_ERR_SUCCESS;
		if (!read && (exists || ec) && !whole) {
			LOG_WARN("config: can't read the file{}, fix counts wait", LogPlain(ec ? " (" + ec.message() + ")" : std::string()));
			fixCountsChanged = true;
			return;
		}
		fixCountsChanged = false;
		if (!read) {
			cfg_details::SaveGuiConfig();
			return;
		}
		change(disk);
		disk.autoswitch_fix = conf_gui()->autoswitch_fix; // счёт исправлений - движка: окно его не меняет
		IFS_LOG(cfg_details::Save_conf(disk));
	};
	// Счёт исправлений вручную изменился - в файл раз в минуту, а не на каждое исправление.
	auto saveFixCounts = [&] { saveMerged([](ProgramConfig&) {}, false); };
	// Слово ушло в список name, вернулось из него или изменился его счёт: в памяти - уже; движку - сразу, в файл - только
	// это слово (его счёт, место в списке и отметка "выучено").
	auto saveWord = [&](const char* name, const std::wstring& word) {
		cfg_details::ApplyGuiConfig();
		const std::string utf8 = StrUtils::Convert(word);
		auto lists = [&](ProgramConfig& c) -> std::pair<std::map<std::string, int>*, std::vector<std::string>*> {
			if (std::string_view(name) == "two_caps_exceptions") return { &c.two_caps_undo, &c.two_caps_exceptions };
			if (std::string_view(name) == "autoswitch_exceptions") return { &c.autoswitch_undo, &c.autoswitch_exceptions };
			return { &c.autoswitch_fix, &c.autoswitch_force };
		};
		saveMerged([&](ProgramConfig& disk) {
			auto [memCounts, memList] = lists(*conf_gui());
			auto [diskCounts, diskList] = lists(disk);
			if (const auto it = memCounts->find(utf8); it != memCounts->end())
				(*diskCounts)[utf8] = it->second;
			else
				diskCounts->erase(utf8);
			auto same = [&](const std::vector<std::string>& from, std::vector<std::string>& to) {
				const bool in = std::ranges::find(from, utf8) != from.end();
				if (!in)
					std::erase(to, utf8);
				else if (std::ranges::find(to, utf8) == to.end())
					to.push_back(utf8);
			};
			same(*memList, *diskList);
			same(conf_gui()->learned[name], disk.learned[name]);
		}, true);
	};
	timer.CycleTimer([&] {
		if (fixCountsChanged) saveFixCounts();
		// Запись для отчёта (SettingsIpc.h) - не дольше часа: о ней могли забыть. Записанное остаётся в файле.
		if (LogSafe() && GetTickCount64() - SettingsIpc::reportSince > SettingsIpc::kReportMs) {
			LOG_ANY("report: an hour has passed, recording stopped");
			SetLogSafe(false);
		}
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
			LOG_ANY("update: note about {} shown {}", LogPlain(state.latest), shown);
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
					// Сам процесс, без оболочки Windows (ShellExecute): она подгружала в FluentSwitcher свои библиотеки, и
					// после первого окна настроек он оставался на 12 МБ больше (gutasiho, форум 08.10.2026).
					std::wstring line = L"\"" + exe + L"\" --settings";
					STARTUPINFOW si{ sizeof(si) };
					PROCESS_INFORMATION pi{};
					if (CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr,
					                   PathUtils::GetPath_folder_noLower2().c_str(), &si, &pi)) {
						CloseHandle(pi.hThread);
						CloseHandle(pi.hProcess);
					}
					else {
						LOG_WARN(L"can't start {} --settings: {}", exe, GetLastError());
					}
				}
				return 0;
			}

			if (msg == WM_UpdateResult) {
				std::unique_ptr<Update::Result> result(reinterpret_cast<Update::Result*>(lParam));
				updates.busy = false;
				if (!result->ok) {
					LOG_WARN("update check: {}", LogPlain(result->error));
					return 0;
				}
				LOG_ANY("update check: latest {}", LogPlain(result->latest));
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
				countToList(conf_gui()->two_caps_undo, conf_gui()->two_caps_exceptions, "two_caps_exceptions", *word,
				            "\"{}\" will not be fixed any more",
				            "It is in the exceptions of TWo INitial CApitals: Settings, Typing");
				saveWord("two_caps_exceptions", *word);
				return 0;
			}

			if (msg == WM_AutoSwitchLearn) {
				// Слово вернули сразу после автопереключения. Как у ДВух ЗАглавных: на третий раз - в исключения
				// автопереключения.
				std::unique_ptr<std::wstring> word(reinterpret_cast<std::wstring*>(lParam));
				LOG_ANY(L"autoswitch: {} switched back", *word);
				countToList(conf_gui()->autoswitch_undo, conf_gui()->autoswitch_exceptions, "autoswitch_exceptions", *word,
				            "\"{}\" will not be switched any more",
				            "It is in the exceptions of the layout auto switch: Settings, Auto switch");
				saveWord("autoswitch_exceptions", *word);
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
					const int undone = uncount(counts, conf_gui()->autoswitch_force, "autoswitch_force", *word);
					LOG_ANY(L"autoswitch: {} fixed back by hand, not counted", *word);
					if (undone == 2)
						saveWord("autoswitch_force", *word);
					else if (undone == 1)
						fixCountsChanged = true;
					return 0;
				}
				LOG_ANY(L"autoswitch: {} fixed by hand", *word);
				const bool learned = countToList(counts, conf_gui()->autoswitch_force, "autoswitch_force", *word,
				                                 "\"{}\" will always be switched",
				                                 "It is in \"Always switch\" of the layout auto switch: Settings, Auto switch");
				if (counts.size() > 300) std::erase_if(counts, [](const auto& c) { return c.second < 2; });
				if (learned)
					saveWord("autoswitch_force", *word);
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
				if (uncount(conf_gui()->autoswitch_undo, conf_gui()->autoswitch_exceptions, "autoswitch_exceptions", *word)) {
					LOG_ANY(L"autoswitch: {} fixed again after switching back, not counted", *word);
					saveWord("autoswitch_exceptions", *word);
				}
				return 0;
			}

			if (msg == WM_TextFixed) {
				layoutSound.OnFix();
				return 0;
			}

			if (msg == WM_ToggleAutoswitch) {
				// Автопереключение вкл./выкл. (форум, 09.10.2026, AlexPORTrb): движку - сразу, в файл - только это поле (как
				// выученные слова - saveMerged). Сочетанием (wParam) - уведомлением у значка, его не видно иначе; из меню у
				// значка - галочка видна и так.
				const bool on = !conf_gui()->autoswitch;
				conf_gui()->autoswitch = on;
				cfg_details::ApplyGuiConfig();
				saveMerged([on](ProgramConfig& disk) { disk.autoswitch = on; }, true);
				LOG_ANY("autoswitch: turned {} {}", on ? "on" : "off", wParam ? "with the hotkey" : "from the menu");
				if (wParam)
					trayIcon.Notify(L"FluentSwitcher", StrUtils::Convert(std::string(LOC(on ? "Auto switch is on" : "Auto switch is off"))),
					                [] { show_main_wind(); });
				return 0;
			}

			if (msg == WM_EnabledChanged) {
				// Включили или выключили сами: в файл только это поле - после перезапуска так же (форум, gutasiho, 10.10.2026).
				const bool on = g_enabled.IsEnabled();
				conf_gui()->enabled = on;
				saveMerged([on](ProgramConfig& disk) { disk.enabled = on; }, true);
				LOG_ANY("enabled {}: kept for the next start", on);
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
	if (fixCountsChanged) saveMerged([](ProgramConfig&) {}, true); // выход: счёт за последнюю минуту не терять
}
