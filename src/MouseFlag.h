// Флаг раскладки на указателе мыши над текстом (mouse_flag; Дмитрий, 16.10.2026, «А давай попробуем!» - как в
// language-indicator, на него дал ссылку AlexPORTrb на форуме): системный указатель-текст (OCR_IBEAM) подменяется таким
// же, с флагом раскладки сбоку (SetSystemCursor; картинка - MouseCursor.h). Рисует его сама Windows - без своего окна и
// без слежки за мышью, ни одного лишнего действия на её движение; виден только там, где указатель и так текстовый: поля
// ввода, текст страниц. Вид флага - как у текстового курсора (caret_flag_set), без прозрачности.
// Указатели Windows возвращаются (SPI_SETCURSORS - схема из параметров Windows) при выключении флага или FluentSwitcher и
// при выходе; после аварийного выхода - при следующем запуске или при удалении программы (метка - файл рядом с ней).
// Только поток интерфейса движка (gui2/main.cpp).
#pragma once

#include "MouseCursor.h"

#include <filesystem>

class MouseFlag {
public:
	MouseFlag() {
		RestoreLeftover();
		m_signature = Signature();
		OnLayout(0);
	}
	~MouseFlag() { Restore(); }
	MouseFlag(const MouseFlag&) = delete;
	MouseFlag& operator=(const MouseFlag&) = delete;

	// Раскладка сменилась; 0 - та же (настройки перечитаны, FluentSwitcher включили или выключили).
	void OnLayout(HKL lay) {
		if (lay) m_lay = lay;
		if (!m_lay) m_lay = ForegroundLayout();
		auto cfg = conf_get_unsafe();
		if (!cfg->mouse_flag || !g_enabled.IsEnabled() || !m_lay) {
			Restore();
			return;
		}
		const std::wstring id = Utils::GetNameForHKL_simple(m_lay);
		const std::wstring key = id + L"|" + StrUtils::Convert(cfg->caret_flag_set) + (cfg->useBritishFlag ? L"|gb" : L"");
		if (m_applied && key == m_key) return;
		Apply(id, key);
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

	// Указатель-текст Windows с флагом раскладки id. nullptr - не вышло.
	HCURSOR Make(const std::wstring& id) {
		if (!m_baseOk) {
			if (!MouseCursor::Capture(LoadCursorW(nullptr, IDC_IBEAM), m_base)) return nullptr;
			m_baseOk = true;
			LOG_ANY("mouse flag: the text pointer of Windows {}x{}, {}", m_base.w, m_base.h,
			        m_base.mono ? "one color" : m_base.alpha ? "with alpha" : "colors");
		}
		const int size = MouseCursor::FlagSize(m_base);
		if (size < 6) return nullptr;
		auto img = IconMgr::Inst().GetImage(id.c_str(), size, false);
		if (!img || !img->IsOk()) return nullptr;
		return MouseCursor::Compose(m_base, MouseCursor::Scaled(img->data, img->width, img->height, size), size);
	}

	void Apply(const std::wstring& id, const std::wstring& key) {
		// Основа - указатель Windows, не наш с прежним флагом: подменённый вернуть, прежде чем снимать.
		if (!m_baseOk && m_applied) {
			SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
			m_applied = false;
		}
		HCURSOR cursor = Make(id);
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
	MouseCursor::Picture m_base;
	bool m_baseOk = false;
};
