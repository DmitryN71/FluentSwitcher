// Главный поток движка: служебное окно (таймеры, команды окна настроек - SettingsIpc.h), значок у часов,
// флажок у текстового курсора и проверка обновлений (Update.h). Окно настроек - этот же exe с --settings, своим процессом (settings/).

#include "TrayIcon.h"
#include "CaretFlag.h"
#include "LayoutSound.h"
#include "SettingsIpc.h"
#include "Update.h"
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
	LayoutSound layoutSound;

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
					layoutSound.OnLayout((HKL)wParam);
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
