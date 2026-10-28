// Флаг раскладки на указателе мыши над текстом (mouse_flag; Дмитрий, 16.10.2026, «А давай попробуем!» - как в
// language-indicator, на него дал ссылку AlexPORTrb на форуме): системный указатель-текст (OCR_IBEAM) подменяется таким
// же, с флагом раскладки сбоку (SetSystemCursor; картинка - MouseCursor.h). Рисует его сама Windows - без своего окна и
// без слежки за мышью, ни одного лишнего действия на её движение; виден только там, где указатель и так текстовый: поля
// ввода, текст страниц. Вид флага - свой (mouse_flag_set; пусто - как у текстового курсора), прозрачность - своя
// (mouse_flag_opacity).
// Указатели Windows возвращаются (SPI_SETCURSORS - схема из параметров Windows) при выключении флага или FluentSwitcher,
// в приложениях из «Не работать в приложениях», при выходе и в конце сеанса Windows; после аварийного выхода - при
// следующем запуске или при удалении программы (метка - файл рядом с ней).
// Только поток интерфейса движка (gui2/main.cpp).
#pragma once

#include "MouseCursor.h"
#include "RemoteDesktop.h"

#include <ShellScalingApi.h>

#include <filesystem>
#include <map>

#pragma comment(lib, "Shcore.lib")

class MouseFlag {
public:
	MouseFlag() {
		s_instance = this;
		RestoreLeftover();
		CreateSessionWindow();
		m_signature = Signature();
		OnLayout(0);
	}
	~MouseFlag() {
		Off();
		DropPictures();
		if (m_session) DestroyWindow(m_session);
		s_instance = nullptr;
	}
	MouseFlag(const MouseFlag&) = delete;
	MouseFlag& operator=(const MouseFlag&) = delete;

	// Раскладка сменилась; 0 - та же (настройки перечитаны, FluentSwitcher включили или выключили). "Ненадолго" - на
	// caret_flag_brief_ms, как флаг у текстового курсора (Дмитрий, 16.10.2026: "почему он не прячется по таймауту?").
	void OnLayout(HKL lay) {
		if (lay) m_lay = lay;
		if (!m_lay) m_lay = Utils::GetFocusedWndInfo().lay;
		auto cfg = conf_get_unsafe();
		const int mode = cfg->mouse_flag;
		if (!mode || !g_enabled.IsEnabled() || !m_lay) {
			Off();
			return;
		}
		HookFocus(mode == 2);
		if (Excluded(cfg)) {
			StopTimer();
			Restore();
			return;
		}
		const std::wstring id = Utils::GetNameForHKL_simple(m_lay);
		const std::string look = cfg->mouse_flag_set.empty() ? cfg->caret_flag_set : cfg->mouse_flag_set;
		const int opacity = std::clamp(cfg->mouse_flag_opacity, 10, 100);
		const uint32_t fill = IconMgr::BadgeFill(m_lay); // цвет плашки (буквы) и полоса CapsLock
		const bool caps = IconMgr::CapsShown();
		const std::wstring key = id + L"|" + StrUtils::Convert(look) + (cfg->useBritishFlag ? L"|gb" : L"") + L"|" +
			std::to_wstring(opacity) + std::format(L"|{:06x}|{}", fill, caps);
		// Картинки для этого флага нет (раскладка без флага в наборе) - не искать её снова на каждое событие.
		if ((!m_applied || key != m_key) && key != m_failedKey) Apply(id, look, opacity, fill, caps, key);
		// Те же пределы, что у флага у текстового курсора (CaretFlag::Brief): строка настройки у них одна.
		if (mode == 2 && m_applied)
			StartTimer((UINT)std::clamp(cfg->caret_flag_brief_ms, 300, 60000));
		else
			StopTimer();
	}

	// Окно впереди сменилось (WM_ForegroundChanged) или в нём - поле ввода (OnFocus): "ненадолго" - показать и там, как флаг
	// у текстового курсора; "всегда" - вдруг пришли в приложение из «Не работать в приложениях» или ушли из него. Раскладка -
	// как у движка (WM_LayNotif): поля с фокусом, не окна впереди - у приложений Магазина впереди рамка ApplicationFrameHost
	// со своей раскладкой (ревью 18.10.2026: флаг показывал не ту раскладку, что значок у часов).
	void OnWindow() {
		if (!conf_get_unsafe()->mouse_flag) return;
		if (HKL lay = Utils::GetFocusedWndInfo().lay) m_lay = lay;
		OnLayout(0);
	}

	// Раз в 2 с (окно движка служебное, WM_SETTINGCHANGE до него не доходит).
	void Watch() {
		// В параметрах Windows сменили схему, размер или цвет указателя, масштаб экрана: основа - снять заново. Наш указатель
		// сначала вернуть (Apply): Windows перечитывает указатели не на всякое значение этих разделов (размер текста - нет), и
		// иначе основой стал бы наш же указатель с флагом - флаг застревал до перезапуска (ревью 18.10.2026).
		const std::wstring signature = Signature();
		if (signature != m_signature) {
			m_signature = signature;
			DropPictures();
			if (!m_applied) return;
			LOG_ANY("mouse flag: the pointers of Windows changed, again");
			m_key.clear();
			OnLayout(0);
			return;
		}
		// Указатели перечитал кто-то другой (свойства мыши, другая программа): наш - уже не наш.
		if (m_applied && !StillOurs()) {
			LOG_ANY("mouse flag: the text pointer was put back by someone else");
			m_applied = false;
			m_key.clear();
			StopTimer();
			std::error_code ec;
			std::filesystem::remove(MarkPath(), ec);
			if (conf_get_unsafe()->mouse_flag == 1) OnLayout(0);
		}
	}

	// Метка от прошлого запуска (вышел, не вернув указатели) - вернуть их. И при удалении программы (--cleanup).
	static void RestoreLeftover() {
		std::error_code ec;
		if (!std::filesystem::exists(MarkPath(), ec)) return;
		SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
		std::filesystem::remove(MarkPath(), ec);
		LOG_ANY("mouse flag: the pointer left by a previous run is put back");
	}

private:
	static constexpr DWORD kOcrIBeam = 32513; // OCR_IBEAM (winuser.h - только с OEMRESOURCE)

	static std::filesystem::path MarkPath() { return PathUtils::GetPath_folder_noLower2() / L"FluentSwitcher.cursor"; }

	// Что задаёт указатели: схема и файлы (Control Panel\Cursors), размер и цвет (Accessibility), масштаб главного экрана.
	static std::wstring Signature() {
		std::wstring s;
		auto add = [&](const wchar_t* path) {
			HKEY key = nullptr;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
			wchar_t name[256];
			BYTE data[1024];
			for (DWORD i = 0;; i++) {
				DWORD nameLen = (DWORD)std::size(name), dataLen = sizeof(data), type = 0;
				const LSTATUS r = RegEnumValueW(key, i, name, &nameLen, nullptr, &type, data, &dataLen);
				if (r == ERROR_NO_MORE_ITEMS) break;
				if (r != ERROR_SUCCESS) continue; // длинное значение - не наше
				s += name;
				s += L'=';
				if (type == REG_SZ || type == REG_EXPAND_SZ)
					s.append((const wchar_t*)data, dataLen / sizeof(wchar_t));
				else if (type == REG_DWORD && dataLen == 4)
					s += std::to_wstring(*(const DWORD*)data);
				s += L';';
			}
			RegCloseKey(key);
		};
		add(L"Control Panel\\Cursors");
		add(L"Software\\Microsoft\\Accessibility");
		UINT dpiX = 96, dpiY = 96; // масштаб сменили - Windows рисует указатели другого размера (ревью 18.10.2026)
		GetDpiForMonitor(MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
		s += L"dpi=" + std::to_wstring(dpiX);
		return s;
	}

	// Отпечаток картинки указателя-текста, какой его сейчас рисует Windows.
	static uint64_t Print() {
		MouseCursor::Picture p;
		if (!MouseCursor::Capture(LoadCursorW(nullptr, IDC_IBEAM), p)) return 0;
		uint64_t h = 1469598103934665603ull; // FNV-1a
		auto mix = [&](const void* data, size_t n) {
			for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t*)data)[i]) * 1099511628211ull;
		};
		mix(&p.w, sizeof(p.w));
		mix(&p.h, sizeof(p.h));
		mix(p.color.data(), p.color.size() * sizeof(uint32_t));
		mix(p.andMask.data(), p.andMask.size());
		return h;
	}
	bool StillOurs() const {
		const uint64_t now = Print();
		return !now || now == m_print; // не прочитать - не трогать
	}

	// Приложение впереди - из «Не работать в приложениях» или окно удалённого рабочего стола, где FluentSwitcher молчит
	// (как флаг у текстового курсора и звуки - Settings.h, IsSkipProgramTop): там указатель Windows (ревью 18.10.2026).
	// Решение - на процесс и настройки: событий фокуса много.
	bool Excluded(const std::shared_ptr<const ProgramConfig>& cfg) {
		const DWORD pid = Utils::GetFocusedWndInfo().pid_top;
		if (pid != m_excludedPid || cfg != m_excludedCfg) {
			m_excludedPid = pid;
			m_excludedCfg = cfg;
			std::wstring path, name;
			m_excluded = pid && Utils::GetProcLowerNameByPid(pid, path, name) == TStatus::SW_ERR_SUCCESS && !name.empty() &&
				(cfg->disableInPrograms.contains(name) || cfg->disableInPrograms.contains(path) ||
				 (!cfg->work_in_remote && RemoteDesktop::IsClient(name)));
			if (m_excluded) LOG_ANY(L"mouse flag: none in {} (Don't work in apps)", LogPlain(name));
		}
		return m_excluded;
	}

	// "Ненадолго" прошло - указатель Windows обратно. Таймер потока (окно движка служебное): его WM_TIMER разбирает цикл
	// сообщений gui2/main.cpp.
	static void CALLBACK OnTimer(HWND, UINT, UINT_PTR, DWORD) {
		if (!s_instance) return;
		s_instance->StopTimer();
		s_instance->Restore();
	}
	void StartTimer(UINT ms) {
		StopTimer();
		m_timer = SetTimer(nullptr, 0, ms, OnTimer);
	}
	void StopTimer() {
		if (m_timer) KillTimer(nullptr, m_timer);
		m_timer = 0;
	}

	// Всё выключить: флаг не показывается.
	void Off() {
		StopTimer();
		HookFocus(false);
		Restore();
	}

	// "Ненадолго" - и после смены поля в том же окне (Дмитрий, 17.10.2026: "хожу по разным полям в одном окне браузера" -
	// флаг не появлялся): фокус перешёл (EVENT_OBJECT_FOCUS; вкладка браузера - тоже). Клик внутри того же поля фокус не
	// меняет - флаг не показывается. Свой перехват: у флага у текстового курсора он есть, только пока тот включён. Без
	// внедрения в чужие процессы: Windows присылает событие циклу сообщений этого потока.
	void HookFocus(bool on) {
		if (on == (m_focusHook != nullptr)) return;
		if (on) {
			m_focusHook = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr, OnFocus, 0, 0,
			                              WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
			if (!m_focusHook) LOG_WARN("mouse flag: SetWinEventHook failed {}", GetLastError());
		} else {
			UnhookWinEvent(m_focusHook);
			m_focusHook = nullptr;
		}
	}
	static void CALLBACK OnFocus(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
		if (s_instance) s_instance->OnWindow();
	}

	// Конец сеанса Windows (выход, перезагрузка, выключение): цикла сообщений и деструктора больше не будет - указатель
	// вернуть и метку убрать сейчас, иначе на каждом следующем запуске срабатывало бы восстановление после сбоя (ревью
	// 18.10.2026). WM_ENDSESSION получают только окна верхнего уровня - своё, невидимое.
	void CreateSessionWindow() {
		WNDCLASSW wc{};
		wc.lpfnWndProc = SessionProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"FluentSwitcher_MouseFlag";
		RegisterClassW(&wc);
		m_session = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
		                            wc.hInstance, nullptr);
	}
	static LRESULT CALLBACK SessionProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		if (msg == WM_ENDSESSION && wParam && s_instance) {
			LOG_ANY("mouse flag: the session ends");
			s_instance->StopTimer();
			s_instance->Restore();
		}
		return DefWindowProcW(wnd, msg, wParam, lParam);
	}

	// Указатель-текст Windows с флагом раскладки id вида look (буквы - на плашке цвета fill; caps - с полосой CapsLock).
	// nullptr - не вышло.
	HCURSOR Make(const std::wstring& id, const std::string& look, int opacity, uint32_t fill, bool caps) {
		if (!m_baseOk) {
			MouseCursor::Picture windows;
			if (!MouseCursor::Capture(LoadCursorW(nullptr, IDC_IBEAM), windows)) return nullptr;
			LOG_ANY("mouse flag: the text pointer of Windows {}x{}, {}", windows.w, windows.h,
			        windows.mono ? "one color" : windows.alpha ? "with alpha" : "colors");
			m_base = MouseCursor::Argb(windows); // в цвет с прозрачностью: так Windows рисует флаг верно
			m_baseOk = true;
		}
		const int size = MouseCursor::FlagSize(m_base);
		if (size < 6) return nullptr;
		// Буквы - вдвое крупнее и уменьшены: на 16 точках, нарисованные сразу, они тонкие и угловатые (Дмитрий,
		// 16.10.2026: "шрифт очень некрасивый"); флаги - своего размера, точка в точку.
		auto img = IconMgr::Inst().GetImage(id.c_str(), LetterIcons::Is(look) ? size * 2 : size, false, look, fill, caps);
		if (!img || !img->IsOk()) return nullptr;
		return MouseCursor::Compose(m_base, MouseCursor::Scaled(img->data, img->width, img->height, size), size, opacity);
	}

	// Готовые указатели - по ключу: "ненадолго" показывает тот же флаг много раз, картинку не собирать каждый раз заново
	// (ревью 18.10.2026). Windows забирает себе копию.
	HCURSOR Made(const std::wstring& id, const std::string& look, int opacity, uint32_t fill, bool caps,
	             const std::wstring& key) {
		if (auto it = m_made.find(key); it != m_made.end()) return it->second;
		HCURSOR cursor = Make(id, look, opacity, fill, caps);
		if (!cursor) return nullptr;
		if (m_made.size() >= 16) DropMade();
		m_made[key] = cursor;
		return cursor;
	}
	void DropMade() {
		for (auto& [key, cursor] : m_made) DestroyCursor(cursor);
		m_made.clear();
	}
	// Основа (указатель Windows) больше не та: всё собранное на ней - заново.
	void DropPictures() {
		m_baseOk = false;
		DropMade();
		m_failedKey.clear();
	}

	void Apply(const std::wstring& id, const std::string& look, int opacity, uint32_t fill, bool caps, const std::wstring& key) {
		// Основа - указатель Windows, не наш с прежним флагом: подменённый вернуть, прежде чем снимать.
		if (!m_baseOk && m_applied) {
			SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
			m_applied = false;
		}
		HCURSOR made = Made(id, look, opacity, fill, caps, key);
		if (!made) {
			LOG_ANY(L"mouse flag: no picture for {}", LogPlain(id));
			m_failedKey = key;
			Restore();
			return;
		}
		HCURSOR cursor = CopyCursor(made);
		if (!cursor) {
			LOG_WARN("mouse flag: CopyCursor failed {}", GetLastError());
			return;
		}
		std::error_code ec;
		const std::filesystem::path mark = MarkPath();
		if (!m_applied) { // метка - до подмены: упадём сразу после - вернём при запуске
			FILE* f = nullptr;
			if (_wfopen_s(&f, mark.c_str(), L"wb") == 0 && f) fclose(f);
		}
		// Windows забирает указатель себе (и сама его уничтожит).
		if (!SetSystemCursor(cursor, kOcrIBeam)) {
			LOG_WARN("mouse flag: SetSystemCursor failed {}", GetLastError());
			DestroyCursor(cursor);
			if (!m_applied) std::filesystem::remove(mark, ec);
			return;
		}
		m_applied = true;
		m_key = key;
		m_print = Print();
		LOG_ANY(L"mouse flag: {}", LogPlain(id));
	}

	void Restore() {
		if (!m_applied) return;
		SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
		m_applied = false;
		m_key.clear();
		std::error_code ec;
		std::filesystem::remove(MarkPath(), ec);
		LOG_ANY("mouse flag: the text pointer of Windows is back");
	}

	HKL m_lay = nullptr;
	bool m_applied = false; // указатель-текст сейчас наш
	std::wstring m_key;     // с каким флагом
	uint64_t m_print = 0;   // и какой он у Windows после подмены (Print)
	std::wstring m_failedKey; // для этого флага картинки нет
	std::wstring m_signature;
	MouseCursor::Picture m_base; // указатель-текст Windows, в цвете с прозрачностью (MouseCursor::Argb)
	bool m_baseOk = false;
	std::map<std::wstring, HCURSOR> m_made; // собранные указатели с флагом (Made)
	UINT_PTR m_timer = 0;        // "ненадолго"
	HWINEVENTHOOK m_focusHook = nullptr; // "ненадолго": смена поля
	HWND m_session = nullptr;    // конец сеанса Windows (CreateSessionWindow)
	DWORD m_excludedPid = 0;     // Excluded: для какого процесса и каких настроек решено
	std::shared_ptr<const ProgramConfig> m_excludedCfg;
	bool m_excluded = false;
	inline static MouseFlag* s_instance = nullptr;
};
