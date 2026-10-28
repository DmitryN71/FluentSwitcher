#pragma once

// Буквы раскладки вместо флага: EN, RU. Значок в трее - буквы цвета текста панели задач на прозрачном фоне, как
// у Windows; флажок у текстового курсора - буквы на плашке с каймой, чтобы их было видно на любом фоне: плашка тёмная или
// своего цвета у раскладки (BadgeColor.h), буквы и кайма - белые или тёмные, какие на ней виднее. Рисует
// Direct2D / DirectWrite в память (без окна), отдаёт RGBA без умножения на альфу - как картинки флагов
// (IconManager.h). По высоте буквы ставятся посередине по тому, где легли их точки (метрики шрифта не точны, и на
// мелком значке промах в полточки виден); по ширине их левый край - в одном столбце у всех раскладок. Только поток
// интерфейса движка.

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include "BadgeColor.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace LetterIcons {

// Буквы в настройках flagsSet (значок в трее) и caret_flag_set (флаг у курсора). Буквы в рамке ("LettersFramed")
// убраны 07.10.2026 (Дмитрий: "флаги в рамке вообще удалим"); в старых настройках они - эти буквы (Settings.cpp).
inline constexpr const char* kPlain = "Letters";

inline bool Is(const std::string& set) { return set == kPlain; }

enum class Style { Plain, Badge };

// "en-US" -> "EN".
inline std::wstring Text(const std::wstring& locale) {
	std::wstring lang = locale.substr(0, locale.find(L'-'));
	for (auto& c : lang) c = (wchar_t)(UINT_PTR)CharUpperW((LPWSTR)(UINT_PTR)c);
	return lang;
}

struct Picture {
	int width = 0, height = 0;
	std::vector<unsigned char> rgba; // без умножения на альфу
};

namespace details {
	using Microsoft::WRL::ComPtr;
	inline ComPtr<ID2D1Factory>& D2D() {
		static ComPtr<ID2D1Factory> f;
		if (!f) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, f.GetAddressOf());
		return f;
	}
	inline ComPtr<IDWriteFactory>& DWrite() {
		static ComPtr<IDWriteFactory> f;
		if (!f) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)f.GetAddressOf());
		return f;
	}
	inline std::wstring Family() {
		static std::wstring family = [] {
			ComPtr<IDWriteFontCollection> fonts;
			if (DWrite() && SUCCEEDED(DWrite()->GetSystemFontCollection(&fonts))) {
				for (auto n : { L"Segoe UI Variable Display", L"Segoe UI" }) {
					UINT32 i = 0;
					BOOL exists = FALSE;
					if (SUCCEEDED(fonts->FindFamilyName(n, &i, &exists)) && exists) return std::wstring(n);
				}
			}
			return std::wstring(L"Segoe UI");
		}();
		return family;
	}
	// Высота заглавных в долях кегля - для кегля; посередине буквы ставятся по их точкам.
	inline float CapHeight(DWRITE_FONT_WEIGHT weight) {
		ComPtr<IDWriteFontCollection> fonts;
		ComPtr<IDWriteFontFamily> family;
		ComPtr<IDWriteFont> font;
		UINT32 i = 0;
		BOOL exists = FALSE;
		DWRITE_FONT_METRICS m{};
		if (SUCCEEDED(DWrite()->GetSystemFontCollection(&fonts)) &&
			SUCCEEDED(fonts->FindFamilyName(Family().c_str(), &i, &exists)) && exists &&
			SUCCEEDED(fonts->GetFontFamily(i, &family)) &&
			SUCCEEDED(family->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) {
			font->GetMetrics(&m);
			if (m.designUnitsPerEm) return (float)m.capHeight / m.designUnitsPerEm;
		}
		return 0.7f;
	}
}

// Буквы в картинке w x h. Plain - значок в трее, цветом текста панели задач: dark - тёмная панель (белые).
// Badge - флажок у курсора, на плашке цвета fill (0xRRGGBB). gray - программа выключена: всё бледное, плашка серая.
inline Picture Render(const std::wstring& text, int w, int h, Style style, bool dark, bool gray,
                      uint32_t fill = BadgeColor::kDark) {
	using Microsoft::WRL::ComPtr;
	Picture pic;
	auto& d2d = details::D2D();
	auto& dw = details::DWrite();
	if (!d2d || !dw || w <= 0 || h <= 0 || text.empty()) return pic;

	BITMAPINFO bi{};
	bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
	void* bits = nullptr;
	HDC screen = GetDC(nullptr);
	HDC mem = CreateCompatibleDC(screen);
	HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	ReleaseDC(nullptr, screen);
	if (!bmp) {
		DeleteDC(mem);
		return pic;
	}
	HGDIOBJ old = SelectObject(mem, bmp);
	const auto* px = (const unsigned char*)bits;

	// Кегль: заглавные не выше 62 % высоты (на плашке - 58 % её внутренней части, без каймы в точку), и буквы не шире
	// места.
	const float stroke = 1.0f;
	const DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
	const float capShare = details::CapHeight(weight);
	const float inner = style == Style::Plain ? (float)h : h - 2 * stroke;
	const float padX = style == Style::Plain ? 0 : stroke + (std::max)(1.0f, std::round(h * 0.14f));
	float size = inner * (style == Style::Badge ? 0.58f : 0.62f) / capShare;
	ComPtr<IDWriteTextFormat> format;
	ComPtr<IDWriteTextLayout> layout;
	auto measure = [&](const std::wstring& t, float s) {
		layout.Reset();
		format.Reset();
		DWRITE_TEXT_METRICS m{};
		if (FAILED(dw->CreateTextFormat(details::Family().c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
		                                DWRITE_FONT_STRETCH_NORMAL, s, L"", &format)) ||
			FAILED(dw->CreateTextLayout(t.c_str(), (UINT32)t.size(), format.Get(), 1000, 1000, &layout)) ||
			FAILED(layout->GetMetrics(&m)))
			return 0.0f;
		return m.width;
	};
	// Не шире места - один кегль для букв всех раскладок: по самым широким из набранного и частых (RU, UK, DE). Иначе
	// "RU", которое шире "EN", уменьшалось сильнее, и на 100 % его буквы выходили на строку ниже (Maz на форуме,
	// 06.10.2026: "буквы EN визуально больше").
	const float room = w - 2 * padX;
	for (const std::wstring& t : { text, std::wstring(L"RU"), std::wstring(L"UK"), std::wstring(L"DE") }) {
		const float width = measure(t, size);
		if (width > room && width > 0) size *= room / width;
	}
	// Чернила надписи по ширине от её начала - по контурам букв (по ширине DirectWrite их здесь не подгоняет к точкам):
	// левый край и ширина. Коробка - 1000 (measure), выступы - от её краёв.
	auto inkOf = [&](const std::wstring& t, float& inkLeft, float& inkWidth) {
		DWRITE_OVERHANG_METRICS o{};
		if (measure(t, size) <= 0 || FAILED(layout->GetOverhangMetrics(&o))) return false;
		inkLeft = -o.left;
		inkWidth = 1000 + o.right + o.left;
		return inkWidth > 0;
	};
	// Левый край букв - в одном столбце у всех раскладок (Дмитрий, 25.10.2026: "ЛЕВАЯ сторона букв R и E всегда с
	// одинаковым отступом от левого края флажка"): при смене раскладки буквы не прыгают. Раньше каждая надпись
	// ставилась посередине сама, и то "RU", то "EN" стояло на точку левее (Ludens на форуме, 25.10.2026: "RU на
	// прикурсорном флаге по-прежнему слегка смещен влево"). Отступ - по частой паре "RU" и "EN", у всех раскладок один,
	// выбирается ниже, по тому, как легли точки; надписи уже (FR, PL) стоят левее середины, шире (UK) - правее.
	ComPtr<IDWriteTextLayout> pairLayout[2];
	float pairLeft[2] = {}, pairWidth = 0;
	int pairCount = 0;
	for (const wchar_t* t : { L"RU", L"EN" }) {
		float l = 0, iw = 0;
		if (!inkOf(t, l, iw)) break;
		pairLayout[pairCount] = layout;
		pairLeft[pairCount++] = l;
		pairWidth += iw;
	}
	float textLeft = 0, textWidth = 0;
	const bool inked = inkOf(text, textLeft, textWidth); // последней: layout - этой надписи

	// Процессором, как меню у значка (FluentMenu.h): видеокарта ради значка в 32 точки - десятки МБ драйвера в движке.
	const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
	ComPtr<ID2D1DCRenderTarget> rt;
	ComPtr<ID2D1SolidColorBrush> brush;
	RECT rc{ 0, 0, w, h };
	int shapeH = h; // высота картинки: плашка бывает на точку ниже h (ниже)
	bool ok = layout && SUCCEEDED(d2d->CreateDCRenderTarget(&props, &rt)) && SUCCEEDED(rt->BindDC(mem, &rc)) &&
		SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0xffffff), &brush));
	if (ok) {
		ComPtr<IDWriteRenderingParams> params;
		if (SUCCEEDED(dw->CreateCustomRenderingParams(1.8f, 0.5f, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT,
		                                              DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, &params)))
			rt->SetTextRenderingParams(params.Get());
		DWRITE_LINE_METRICS line{};
		UINT32 lines = 0;
		layout->GetLineMetrics(&line, 1, &lines);
		const float cap = size * capShare;
		float x = 0, y = std::round((h + cap) / 2) - line.baseline;

		// На плашке буквы и кайма - белые или тёмные, какие на её цвете виднее (на тёмной - белые, как было).
		const uint32_t plate = gray ? BadgeColor::Gray(fill) : fill;
		const uint32_t onPlate = BadgeColor::Letters(plate);
		D2D1_COLOR_F ink = style == Style::Badge ? D2D1::ColorF(onPlate) : dark ? D2D1::ColorF(0xffffff) : D2D1::ColorF(0x1b1b1b);
		const float alpha = gray ? 0.45f : 1.0f;
		auto draw = [&](bool shape, IDWriteTextLayout* drawn = nullptr) {
			rt->BeginDraw();
			rt->Clear(D2D1::ColorF(0, 0, 0, 0));
			rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
			if (shape && style == Style::Badge) {
				const float r = shapeH * 0.28f;
				brush->SetColor(D2D1::ColorF(plate));
				rt->FillRoundedRectangle({ { 0.5f, 0.5f, w - 0.5f, shapeH - 0.5f }, r, r }, brush.Get());
				brush->SetColor(D2D1::ColorF(onPlate, 0.28f));
				rt->DrawRoundedRectangle({ { 0.5f, 0.5f, w - 0.5f, shapeH - 0.5f }, r, r }, brush.Get(), 1.0f);
			}
			D2D1_COLOR_F letters = ink;
			letters.a = shape ? alpha : 1.0f; // одни буквы - чтобы мерить: и у бледных (выключено) там же
			brush->SetColor(letters);
			rt->DrawTextLayout({ x, y }, drawn ? drawn : layout.Get(), brush.Get());
			const bool done = SUCCEEDED(rt->EndDraw());
			GdiFlush(); // точки - в памяти картинки, прежде чем их читать
			return done;
		};
		// Самая заметная точка каждого столбца - одних букв, нарисованных сейчас.
		auto columns = [&] {
			std::vector<unsigned> column(w);
			for (int yy = 0; yy < h; yy++)
				for (int xx = 0; xx < w; xx++) column[xx] = (std::max)(column[xx], (unsigned)px[(yy * w + xx) * 4 + 3]);
			return column;
		};
		// x, при котором левый край букв - ровно на столбце m: по контурам (inkLeft) с поправкой на восьмые доли точки -
		// столбец m заметнее всего, а левее него пусто. Контуры переменного шрифта DirectWrite меряет не точно: "UK"
		// вставало на полточки левее "RU".
		auto alignAt = [&](IDWriteTextLayout* drawn, float inkLeft, int m) {
			float found = m - inkLeft;
			int strongest = -1;
			for (int k = -4; k <= 4; k++) {
				x = m - inkLeft + k / 8.0f;
				if (!draw(false, drawn)) continue;
				const auto column = columns();
				if (m > 0 && column[m - 1] > 12) continue;
				if ((int)column[m] > strongest) strongest = (int)column[m], found = x;
			}
			return found;
		};
		// Насколько буквы, нарисованные при этом x, стоят правее середины (минус - левее): по тому, как легли точки, крайний
		// столбец - по его самой заметной точке, бледный - буквы на глаз начинаются (кончаются) дальше.
		auto aside = [&](IDWriteTextLayout* drawn) {
			if (!draw(false, drawn)) return 0.0;
			const auto column = columns();
			int l = -1, r = -1;
			for (int xx = 0; xx < w; xx++)
				if (column[xx] > 32) {
					if (l < 0) l = xx;
					r = xx;
				}
			if (l < 0) return 0.0;
			const double before = l + 1 - column[l] / 255.0, after = w - r - column[r] / 255.0;
			return (before - after) / 2;
		};
		// Отступ слева - из двух ближайших к тому, при котором пара по контурам стоит посередине: тот, при котором дальняя
		// от середины из двух надписей отходит от неё меньше (по одним контурам "RU" на 125 %, "Крупный" - 30 точек,
		// уходило вправо почти на точку). Один на размер: запомнен (на поток - рисуют и окно настроек, и флаг на
		// указателе).
		thread_local std::map<std::tuple<int, int, int, long>, int> margins;
		const auto key = std::make_tuple(w, h, (int)style, std::lround(size * 100));
		int margin = (int)std::lround((w - (inked ? textWidth : 0)) / 2);
		if (const auto known = margins.find(key); known != margins.end())
			margin = known->second;
		else if (pairCount == 2) {
			const int closest = (int)std::floor((w - pairWidth / 2) / 2);
			double fewest = 1e9;
			for (int m : { closest + 1, closest }) { // поровну - правее: "RU" стояло левее
				double farthest = 0;
				for (int i = 0; i < 2; i++) {
					x = alignAt(pairLayout[i].Get(), pairLeft[i], m);
					farthest = (std::max)(farthest, std::abs(aside(pairLayout[i].Get())));
				}
				if (farthest < fewest - 0.01) fewest = farthest, margin = m;
			}
			margins[key] = margin;
		}
		// Надписи шире не хватает места до правого края (на плашке - до точки перед каймой) - она посередине.
		if (margin + textWidth > w - (style == Style::Plain ? 0 : stroke + 1)) margin = (int)std::lround((w - textWidth) / 2);
		x = inked ? alignAt(nullptr, textLeft, margin) : 0;
		// По ширине левый край букв - ровно на точке отступа (x выше): левый штрих R, E, D, U чёткий и у всех раскладок
		// в одном столбце. По высоте сначала одни буквы - со сдвигом на восьмую долю точки, при котором горизонтальные
		// штрихи ложатся на точки чётче всего (больше всего непрозрачных точек: сумма квадратов непрозрачности). Без
		// этого одни буквы выходили чёткими, а другие, попав между точками, - бледнее и ниже: на 100 % "RU" на строку ниже
		// "EN" (Maz на форуме, 06.10.2026: "буквы EN визуально больше"). Потом сдвиг на целые точки, чтобы буквы стояли
		// посередине по высоте, и всё начисто.
		const float baseY = std::floor(y);
		double best = -1;
		float bestY = baseY;
		int top = h, bottom = -1;
		auto tryAt = [&](float ty) {
			y = ty;
			if (!draw(false)) return;
			// Верх и низ букв - по заметным точкам: бледная строка не делает буквы выше.
			double score = 0;
			int t = h, b = -1;
			for (int yy = 0; yy < h; yy++) {
				for (int xx = 0; xx < w; xx++) {
					const unsigned a = px[(yy * w + xx) * 4 + 3];
					score += (double)a * a;
					if (a > 96) {
						t = (std::min)(t, yy);
						b = (std::max)(b, yy);
					}
				}
			}
			if (score > best) {
				best = score;
				bestY = ty;
				top = t, bottom = b;
			}
		};
		for (int j = 0; j < 8; j++) tryAt(baseY + j / 8.0f);
		ok = best >= 0;
		if (ok) {
			y = bestY;
			if (bottom >= 0) {
				const int inkH = bottom - top + 1;
				// Не делится поровну: на плашке - она на точку ниже, и поля над буквами и под ними равны (Ludens на форуме,
				// 25.10.2026: "верхнее поле над буквами — три пикселя, нижнее — четыре. Убрать бы снизу один пиксель");
				// высота одна на размер - у всех раскладок. В трее - буквы выше на полточки.
				if (style == Style::Badge) {
					thread_local std::map<std::tuple<int, int, int, long>, int> heights;
					int& height = heights[key];
					if (!height) height = (h - inkH) % 2 ? h - 1 : h;
					shapeH = height;
				}
				y += (float)((shapeH - inkH) / 2 - top);
			}
			ok = draw(true);
		}
	}
	if (ok) {
		pic.width = w;
		pic.height = shapeH;
		pic.rgba.resize((size_t)w * shapeH * 4);
		for (int i = 0; i < w * shapeH; i++) {
			const unsigned a = px[i * 4 + 3];
			auto un = [a](unsigned c) { return (unsigned char)(a ? (std::min)(255u, c * 255 / a) : 0); };
			pic.rgba[i * 4 + 0] = un(px[i * 4 + 2]);
			pic.rgba[i * 4 + 1] = un(px[i * 4 + 1]);
			pic.rgba[i * 4 + 2] = un(px[i * 4 + 0]);
			pic.rgba[i * 4 + 3] = (unsigned char)a;
		}
	}
	SelectObject(mem, old);
	DeleteObject(bmp);
	DeleteDC(mem);
	return pic;
}

}
