// Флаг раскладки на указателе мыши над текстом (mouse_flag; Дмитрий, 16.10.2026, «А давай попробуем!» - как в
// language-indicator, на него дал ссылку AlexPORTrb на форуме): системный указатель-текст (OCR_IBEAM) подменяется таким
// же, с флагом раскладки сбоку (SetSystemCursor; картинка - MouseCursor.h). Рисует его сама Windows - без своего окна и
// без слежки за мышью, ни одного лишнего действия на её движение; виден только там, где указатель и так текстовый: поля
// ввода, текст страниц. Вид флага - свой (mouse_flag_set; пусто - как у текстового курсора), прозрачность - своя
// (mouse_flag_opacity).
// Указатели Windows возвращаются (SPI_SETCURSORS - схема из параметров Windows) при выключении флага или FluentSwitcher и
// при выходе; после аварийного выхода - при следующем запуске или при удалении программы (метка - файл рядом с ней).
// Только поток интерфейса движка (gui2/main.cpp).
#pragma once

#include "MouseCursor.h"

#include <filesystem>

class MouseFlag {
public:
	MouseFlag() {
		s_instance = this;
		RestoreLeftover();
		m_signature = Signature();
		OnLayout(0);
	}
	~MouseFlag() {
		StopTimer();
		HookFocus(false);
		Restore();
		s_instance = nullptr;
	}
	MouseFlag(const MouseFlag&) = delete;
	MouseFlag& operator=(const MouseFlag&) = delete;

	// Раскладка сменилась; 0 - та же (настройки перечитаны, FluentSwitcher включили или выключили). "Ненадолго" - на
	// caret_flag_brief_ms, как флаг у текстового курсора (Дмитрий, 16.10.2026: "почему он не прячется по таймауту?").
	void OnLayout(HKL lay) {
		if (lay) m_lay = lay;
		if (!m_lay) m_lay = ForegroundLayout();
		auto cfg = conf_get_unsafe();
		const int mode = cfg->mouse_flag;
		if (!mode || !g_enabled.IsEnabled() || !m_lay) {
			StopTimer();
			HookFocus(false);
			Restore();
			return;
		}
		HookFocus(mode == 2);
		const std::wstring id = Utils::GetNameForHKL_simple(m_lay);
		const std::string look = cfg->mouse_flag_set.empty() ? cfg->caret_flag_set : cfg->mouse_flag_set;
		const int opacity = std::clamp(cfg->mouse_flag_opacity, 10, 100);
		const std::wstring key = id + L"|" + StrUtils::Convert(look) + (cfg->useBritishFlag ? L"|gb" : L"") + L"|" +
			std::to_wstring(opacity);
		if (!m_applied || key != m_key) Apply(id, look, opacity, key);
		if (mode == 2 && m_applied)
			StartTimer((UINT)std::clamp(cfg->caret_flag_brief_ms, 500, 30000));
		else
			StopTimer();
	}

	// Окно впереди сменилось (WM_ForegroundChanged) или в нём - поле ввода (OnFocus): "ненадолго" - показать и там, как флаг
	// у текстового курсора; раскладка - того окна (WM_LayNotif приходит, только когда она другая).
	void OnWindow() {
		if (conf_get_unsafe()->mouse_flag != 2) return;
		if (HKL lay = ForegroundLayout()) m_lay = lay;
		OnLayout(0);
	}

	// Раз в 2 с (окно движка служебное, WM_SETTINGCHANGE до него не доходит): в параметрах Windows сменили схему, размер или
	// цвет указателя - Windows перечитала указатели, нашего больше нет. Основа - уже новый, подменить заново.
	void Watch() {
		const std::wstring signature = Signature();
		if (signature == m_signature) return;
		m_signature = signature;
		m_baseOk = false;
		if (!m_applied) return;
		LOG_ANY("mouse flag: the pointers of Windows changed, again");
		m_applied = false;
		OnLayout(0);
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

	static HKL ForegroundLayout() {
		HWND fg = GetForegroundWindow();
		return GetKeyboardLayout(fg ? GetWindowThreadProcessId(fg, nullptr) : 0);
	}

	// Что в параметрах Windows задаёт указатели: схема и файлы (Control Panel\Cursors), размер и цвет (Accessibility).
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
		return s;
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

	// Указатель-текст Windows с флагом раскладки id вида look. nullptr - не вышло.
	HCURSOR Make(const std::wstring& id, const std::string& look, int opacity) {
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
		auto img = IconMgr::Inst().GetImage(id.c_str(), LetterIcons::Is(look) ? size * 2 : size, false, look);
		if (!img || !img->IsOk()) return nullptr;
		return MouseCursor::Compose(m_base, MouseCursor::Scaled(img->data, img->width, img->height, size), size, opacity);
	}

	void Apply(const std::wstring& id, const std::string& look, int opacity, const std::wstring& key) {
		// Основа - указатель Windows, не наш с прежним флагом: подменённый вернуть, прежде чем снимать.
		if (!m_baseOk && m_applied) {
			SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
			m_applied = false;
		}
		HCURSOR cursor = Make(id, look, opacity);
		if (!cursor) {
			LOG_ANY(L"mouse flag: no picture for {}", LogPlain(id));
			Restore();
			return;
		}
		std::error_code ec;
		const std::filesystem::path mark = MarkPath();
		FILE* f = nullptr; // метка - до подмены: упадём сразу после - вернём при запуске
		if (_wfopen_s(&f, mark.c_str(), L"wb") == 0 && f) fclose(f);
		// Windows забирает указатель себе (и сама его уничтожит).
		if (!SetSystemCursor(cursor, kOcrIBeam)) {
			LOG_WARN("mouse flag: SetSystemCursor failed {}", GetLastError());
			DestroyCursor(cursor);
			if (!m_applied) std::filesystem::remove(mark, ec);
			return;
		}
		m_applied = true;
		m_key = key;
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
	std::wstring m_signature;
	MouseCursor::Picture m_base; // указатель-текст Windows, в цвете с прозрачностью (MouseCursor::Argb)
	bool m_baseOk = false;
	UINT_PTR m_timer = 0;        // "ненадолго"
	HWINEVENTHOOK m_focusHook = nullptr; // "ненадолго": смена поля
	inline static MouseFlag* s_instance = nullptr;
};
