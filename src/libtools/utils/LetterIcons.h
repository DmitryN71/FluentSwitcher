#pragma once

// Буквы раскладки вместо флага: EN / RU или ENG / RUS, как пишет сама Windows у часов (ISO 639-2). Значок у часов -
// буквы цвета текста панели задач на прозрачном фоне, как у Windows; флажок у текстового курсора - буквы на тёмной
// плашке со светлой каймой, чтобы их было видно на любом фоне. Рисует Direct2D / DirectWrite в память (без окна),
// отдаёт RGBA без умножения на альфу - как картинки флагов (IconManager.h). Только поток интерфейса движка.

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

// Наборы в настройке flagsSet: две буквы и три.
inline constexpr const char* kTwo = "Letters";
inline constexpr const char* kThree = "Letters3";

inline bool Is(const std::string& set) { return set == kTwo || set == kThree; }

// "en-US" -> "EN" (или "ENG" для трёх букв, как у Windows: код языка ISO 639-2).
inline std::wstring Text(const std::wstring& locale, bool three) {
	std::wstring lang = locale.substr(0, locale.find(L'-'));
	if (three) {
		wchar_t buf[16] = {};
		if (GetLocaleInfoEx(locale.c_str(), LOCALE_SISO639LANGNAME2, buf, 16) > 0 && buf[0]) lang = buf;
	}
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
	// Высота заглавных шрифта в долях кегля: по ней буквы ставятся ровно посередине.
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

// Буквы в картинке w x h. badge - на плашке (флажок у курсора), иначе на прозрачном (значок у часов) цветом текста
// панели задач: dark - тёмная панель (буквы белые). gray - программа выключена: буквы бледные.
inline Picture Render(const std::wstring& text, int w, int h, bool badge, bool dark, bool gray) {
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

	const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
	ComPtr<ID2D1DCRenderTarget> rt;
	ComPtr<ID2D1SolidColorBrush> brush;
	ComPtr<IDWriteTextFormat> format;
	ComPtr<IDWriteTextLayout> layout;
	RECT rc{ 0, 0, w, h };
	const DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
	// Кегль: буквы во всю ширину (с полями на плашке), но заглавные не выше 62 % высоты.
	const float padX = badge ? h * 0.22f : 0, capShare = details::CapHeight(weight);
	float size = h * 0.62f / capShare;
	auto measure = [&](float s) {
		layout.Reset();
		format.Reset();
		DWRITE_TEXT_METRICS m{};
		if (FAILED(dw->CreateTextFormat(details::Family().c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
		                                DWRITE_FONT_STRETCH_NORMAL, s, L"", &format)) ||
			FAILED(dw->CreateTextLayout(text.c_str(), (UINT32)text.size(), format.Get(), 1000, 1000, &layout)) ||
			FAILED(layout->GetMetrics(&m)))
			return 0.0f;
		return m.width;
	};
	const float room = w - 2 * padX;
	float tw = measure(size);
	if (tw > room && tw > 0) {
		size *= room / tw;
		tw = measure(size);
	}
	if (layout && SUCCEEDED(d2d->CreateDCRenderTarget(&props, &rt)) && SUCCEEDED(rt->BindDC(mem, &rc)) &&
		SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0xffffff), &brush))) {
		DWRITE_LINE_METRICS line{};
		UINT32 lines = 0;
		layout->GetLineMetrics(&line, 1, &lines);
		// Заглавные - посередине по высоте, строка букв - по целой точке (чётче).
		const float cap = size * capShare;
		const float baseline = std::round((h + cap) / 2);
		const float x = (w - tw) / 2, y = baseline - line.baseline;

		ComPtr<IDWriteRenderingParams> params;
		if (SUCCEEDED(dw->CreateCustomRenderingParams(1.8f, 0.5f, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT,
		                                              DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, &params)))
			rt->SetTextRenderingParams(params.Get());
		rt->BeginDraw();
		rt->Clear(D2D1::ColorF(0, 0, 0, 0));
		rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
		D2D1_COLOR_F ink = dark ? D2D1::ColorF(0xffffff) : D2D1::ColorF(0x1b1b1b);
		if (badge) {
			const float r = h * 0.28f;
			brush->SetColor(D2D1::ColorF(0x202020));
			rt->FillRoundedRectangle({ { 0.5f, 0.5f, w - 0.5f, h - 0.5f }, r, r }, brush.Get());
			brush->SetColor(D2D1::ColorF(0xffffff, 0.28f));
			rt->DrawRoundedRectangle({ { 0.5f, 0.5f, w - 0.5f, h - 0.5f }, r, r }, brush.Get(), 1.0f);
			ink = D2D1::ColorF(0xffffff);
		}
		if (gray) ink.a = 0.45f;
		brush->SetColor(ink);
		rt->DrawTextLayout({ x, y }, layout.Get(), brush.Get());
		if (SUCCEEDED(rt->EndDraw())) {
			pic.width = w;
			pic.height = h;
			pic.rgba.resize((size_t)w * h * 4);
			const auto* src = (const unsigned char*)bits;
			for (int i = 0; i < w * h; i++) {
				const unsigned a = src[i * 4 + 3];
				auto un = [a](unsigned c) { return (unsigned char)(a ? (std::min)(255u, c * 255 / a) : 0); };
				pic.rgba[i * 4 + 0] = un(src[i * 4 + 2]);
				pic.rgba[i * 4 + 1] = un(src[i * 4 + 1]);
				pic.rgba[i * 4 + 2] = un(src[i * 4 + 0]);
				pic.rgba[i * 4 + 3] = (unsigned char)a;
			}
		}
	}
	SelectObject(mem, old);
	DeleteObject(bmp);
	DeleteDC(mem);
	return pic;
}

}
