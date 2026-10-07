#pragma once

// Буквы раскладки вместо флага: EN, RU. Значок в трее - буквы цвета текста панели задач на прозрачном фоне, как
// у Windows; флажок у текстового курсора - буквы на тёмной плашке со светлой каймой, чтобы их было видно на любом
// фоне. Рисует
// Direct2D / DirectWrite в память (без окна), отдаёт RGBA без умножения на альфу - как картинки флагов
// (IconManager.h). Буквы ставятся посередине по тому, где легли их точки (метрики шрифта не точны, и на мелком
// значке промах в полточки виден). Только поток интерфейса движка.

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <string>
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
// Badge - флажок у курсора, на плашке. gray - программа выключена: всё бледное.
inline Picture Render(const std::wstring& text, int w, int h, Style style, bool dark, bool gray) {
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
	const float tw = measure(text, size);

	const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
	ComPtr<ID2D1DCRenderTarget> rt;
	ComPtr<ID2D1SolidColorBrush> brush;
	RECT rc{ 0, 0, w, h };
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
		float x = (w - tw) / 2, y = std::round((h + cap) / 2) - line.baseline;

		D2D1_COLOR_F ink = style == Style::Badge || dark ? D2D1::ColorF(0xffffff) : D2D1::ColorF(0x1b1b1b);
		const float alpha = gray ? 0.45f : 1.0f;
		auto draw = [&](bool shape) {
			rt->BeginDraw();
			rt->Clear(D2D1::ColorF(0, 0, 0, 0));
			rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
			if (shape && style == Style::Badge) {
				const float r = h * 0.28f;
				brush->SetColor(D2D1::ColorF(0x202020));
				rt->FillRoundedRectangle({ { 0.5f, 0.5f, w - 0.5f, h - 0.5f }, r, r }, brush.Get());
				brush->SetColor(D2D1::ColorF(0xffffff, 0.28f));
				rt->DrawRoundedRectangle({ { 0.5f, 0.5f, w - 0.5f, h - 0.5f }, r, r }, brush.Get(), 1.0f);
			}
			D2D1_COLOR_F letters = ink;
			letters.a = alpha;
			brush->SetColor(letters);
			rt->DrawTextLayout({ x, y }, layout.Get(), brush.Get());
			const bool done = SUCCEEDED(rt->EndDraw());
			GdiFlush(); // точки - в памяти картинки, прежде чем их читать
			return done;
		};
		// Сначала одни буквы - со сдвигом на долю точки, при котором их штрихи ложатся на точки чётче всего (больше
		// всего непрозрачных точек: сумма квадратов непрозрачности). Без этого одни буквы выходили чёткими, а другие,
		// попав между точками, - бледнее и ниже: на 100 % "RU" на строку ниже "EN" (Maz на форуме, 06.10.2026: "буквы EN
		// визуально больше"). Потом сдвиг на целые точки, чтобы буквы стояли посередине (не делится поровну - выше и
		// левее на полточки), и всё начисто.
		// Сдвиг - по восьмой доле точки, сначала по ширине (вертикальные штрихи), потом по высоте (горизонтальные).
		const float baseX = std::floor(x), baseY = std::floor(y);
		double best = -1;
		float bestX = baseX, bestY = baseY;
		int top = h, bottom = -1, left = w, right = -1;
		auto tryAt = [&](float tx, float ty) {
			x = tx;
			y = ty;
			if (!draw(false)) return;
			// Края букв: верх и низ - по заметным точкам (бледная строка не делает буквы выше), левый и правый - и по
			// бледным: штрих между точками ("U" на 100 %) - тоже буква, иначе надпись вставала не посередине.
			double score = 0;
			int t = h, b = -1, l = w, r = -1;
			for (int yy = 0; yy < h; yy++) {
				for (int xx = 0; xx < w; xx++) {
					const unsigned a = px[(yy * w + xx) * 4 + 3];
					score += (double)a * a;
					if (a > 96) {
						t = (std::min)(t, yy);
						b = (std::max)(b, yy);
					}
					if (a > 32) {
						l = (std::min)(l, xx);
						r = (std::max)(r, xx);
					}
				}
			}
			if (score > best) {
				best = score;
				bestX = tx;
				bestY = ty;
				top = t, bottom = b, left = l, right = r;
			}
		};
		for (int i = 0; i < 8; i++) tryAt(baseX + i / 8.0f, baseY);
		const float columnX = bestX;
		for (int j = 1; j < 8; j++) tryAt(columnX, baseY + j / 8.0f);
		ok = best >= 0;
		if (ok) {
			x = bestX;
			y = bestY;
			if (bottom >= 0) {
				const int inkW = right - left + 1, inkH = bottom - top + 1;
				x += (float)((w - inkW) / 2 - left);
				y += (float)((h - inkH) / 2 - top);
			}
			ok = draw(true);
		}
	}
	if (ok) {
		pic.width = w;
		pic.height = h;
		pic.rgba.resize((size_t)w * h * 4);
		for (int i = 0; i < w * h; i++) {
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
