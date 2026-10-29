#pragma once

// Цвет плашки с буквами раскладки - у каждой раскладки свой (layouts_info[].badge; форум, 28.10.2026: из девяти вариантов
// https://dmitryn71.github.io/FluentSwitcher/badges/ больше всех голосов у «свой цвет у языка»). Значение в настройках:
// "" - тёмная, как было всегда; "windows" - цвет акцента Windows; "#rrggbb" - из палитры ниже или свой (сетка цветов Windows
// в окне настроек). Буквы - белые или тёмные, какие на этом цвете виднее. И полоса CapsLock на флаге (flag_capslock). Нужен
// и движку (LetterIcons.h, IconManager.h), и окну настроек (образец плашки у выбора цвета).

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "dwmapi.lib")

namespace BadgeColor {

// Цвета - 0xRRGGBB.
inline constexpr uint32_t kDark = 0x202020;
inline constexpr uint32_t kWhiteLetters = 0xffffff, kDarkLetters = 0x1c1c1c;
inline constexpr const char* kWindows = "windows";
// Полоса CapsLock - оранжевая, как на странице вариантов (вариант 8).
inline constexpr uint32_t kCaps = 0xf5a623;

// Палитра окна настроек: значение в файле и его цвет. Синий, красный, жёлтый - со страницы вариантов (jexx85 на форуме,
// 28.10.2026: «именно в указанных цветах»); серые - для тех, кто голосовал за оттенки серого.
struct Entry {
	const char* key;
	uint32_t rgb;
};
inline constexpr Entry kPalette[] = {
	{ "", kDark },           // тёмная
	{ "#6b6b6b", 0x6b6b6b }, // серая
	{ "#dcdcdc", 0xdcdcdc }, // светлая
	{ "#2f5d9a", 0x2f5d9a }, // синяя
	{ "#a33a3a", 0xa33a3a }, // красная
	{ "#c99a06", 0xc99a06 }, // жёлтая
	{ "#2e7d4f", 0x2e7d4f }, // зелёная
	{ "#6b4fa0", 0x6b4fa0 }, // фиолетовая
};

// Цвет акцента Windows - сам акцент, не светлее и не темнее (так его рисуют приложения для текста, а плашка - не текст).
// Прочитанное помнится секунду: флаг у курсора спрашивает цвет на каждое движение каретки.
inline bool WindowsAccent(uint32_t& rgb) {
	thread_local ULONGLONG readAt = 0;
	thread_local bool known = false;
	thread_local uint32_t last = 0;
	const ULONGLONG now = GetTickCount64();
	if (readAt && now - readAt < 1000) {
		rgb = last;
		return known;
	}
	readAt = now;
	known = false;
	BYTE palette[32] = {};
	DWORD size = sizeof(palette);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", L"AccentPalette",
	                 RRF_RT_REG_BINARY, nullptr, palette, &size) == ERROR_SUCCESS && size >= 32) {
		const BYTE* c = palette + 3 * 4; // восемь цветов R, G, B, A: три светлее, акцент, три темнее, ещё один
		last = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
		known = true;
	} else {
		DWORD argb = 0;
		BOOL opaque = FALSE;
		if (SUCCEEDED(DwmGetColorizationColor(&argb, &opaque))) { // нет палитры - цвет рамок окон
			last = argb & 0xffffff;
			known = true;
		}
	}
	if (known) rgb = last;
	return known;
}

// "#rrggbb" -> цвет; не то - false.
inline bool Parse(const std::string& key, uint32_t& rgb) {
	if (key.size() != 7 || key[0] != '#') return false;
	uint32_t value = 0;
	for (size_t i = 1; i < 7; i++) {
		const char c = key[i];
		const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
		if (digit < 0) return false;
		value = value << 4 | (uint32_t)digit;
	}
	rgb = value;
	return true;
}

// Цвет плашки для значения key; не разобрать (испорчено, акцента нет) - тёмная.
inline uint32_t Resolve(const std::string& key) {
	uint32_t rgb = kDark;
	if (key == kWindows) return WindowsAccent(rgb) ? rgb : kDark;
	return Parse(key, rgb) ? rgb : kDark;
}

// Яркость для глаза (как в WCAG): 0 - чёрный, 1 - белый.
inline double Luminance(uint32_t rgb) {
	auto linear = [](uint32_t c) {
		const double s = c / 255.0;
		return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
	};
	return 0.2126 * linear(rgb >> 16 & 0xff) + 0.7152 * linear(rgb >> 8 & 0xff) + 0.0722 * linear(rgb & 0xff);
}
inline double Contrast(uint32_t a, uint32_t b) {
	const double la = Luminance(a), lb = Luminance(b);
	return ((std::max)(la, lb) + 0.05) / ((std::min)(la, lb) + 0.05);
}

// Буквы на плашке этого цвета: белые или тёмные - какие виднее (на тёмной, как было, - белые).
inline uint32_t Letters(uint32_t fill) {
	return Contrast(kWhiteLetters, fill) >= Contrast(kDarkLetters, fill) ? kWhiteLetters : kDarkLetters;
}

// Серый той же яркости - программа выключена (флаги в это время тоже серые).
inline uint32_t Gray(uint32_t rgb) {
	const uint32_t l = (uint32_t)std::lround(0.299 * (rgb >> 16 & 0xff) + 0.587 * (rgb >> 8 & 0xff) + 0.114 * (rgb & 0xff));
	return l << 16 | l << 8 | l;
}

// CapsLock включён (flag_capslock): над картинкой - флагом или плашкой с буквами - оранжевая полоса через пустую строку,
// сама картинка не тронута (gutasiho на форуме, 28.10.2026: «CapsLock должен хорошо просматриваться на индикаторах любой
// раскладки»). Поверх флага полоса закрывала его верхнюю полосу - российский становился жёлто-сине-красным, а на плашке
// съедала поле над буквами (снимки Дмитрия, 29.10.2026: «почему флаг становится выше, а у букв полоса отнимает место?»):
// теперь у обоих одинаково. Толщина - 20 % высоты флага, не тоньше трёх точек (в 14 %, как на странице вариантов, на
// обычном размере - одна заметная точка); ширина - как у самого флага, без тени и полупрозрачного края (у флага с
// переливом полоса выходила на тень справа). Свободных строк в картинке не хватает (плашка - впритык; у Flagpack картинка
// квадратная, флаг в ней ниже на четверть - хватает) - она выше: h растёт. Полоса, строка и флаг - посередине по высоте.
// rgba - без умножения на альфу, w x h.
inline void CapsStripe(std::vector<unsigned char>& rgba, int w, int& h) {
	if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * h * 4) return;
	const size_t row = (size_t)w * 4;
	int top = h, bottom = -1;                                      // видимое, с тенью: его не обрезать
	int solidTop = h, solidBottom = -1, left = w, right = -1;      // сам флаг
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			const unsigned a = rgba[y * row + x * 4 + 3];
			if (a > 40) {
				top = (std::min)(top, y);
				bottom = (std::max)(bottom, y);
			}
			if (a > 200) {
				solidTop = (std::min)(solidTop, y);
				solidBottom = (std::max)(solidBottom, y);
				left = (std::min)(left, x);
				right = (std::max)(right, x);
			}
		}
	if (bottom < 0) return;
	if (right < 0) { // непрозрачного нет - по видимому
		solidTop = top, solidBottom = bottom, left = 0, right = w - 1;
	}
	const int thick = (std::max)(3, (int)std::lround((solidBottom - solidTop + 1) * 0.2));
	const int group = thick + 1 + (bottom - top + 1);
	if (group > h) { // выше на недостающие строки - сверху
		const int more = group - h;
		rgba.insert(rgba.begin(), (size_t)more * row, 0);
		h += more, top += more, bottom += more;
	}
	// Флаг сдвигается на shift строк (вниз - плюс); ниже видимого - только почти прозрачное.
	const int start = (h - group) / 2, shift = start + thick + 1 - top;
	unsigned char* data = rgba.data();
	if (shift > 0) {
		std::memmove(data + shift * row, data, (h - shift) * row);
		std::memset(data, 0, shift * row);
	} else if (shift < 0) {
		std::memmove(data, data - shift * row, (h + shift) * row);
		std::memset(data + (h + shift) * row, 0, -shift * row);
	}
	for (int y = start; y < start + thick; y++)
		for (int x = left; x <= right; x++) {
			unsigned char* p = data + y * row + x * 4;
			p[0] = kCaps >> 16 & 0xff;
			p[1] = kCaps >> 8 & 0xff;
			p[2] = kCaps & 0xff;
			p[3] = 255;
		}
}

}
