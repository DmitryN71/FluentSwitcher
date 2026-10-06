#pragma once

// Буквы раскладки вместо флага: EN, RU. Значок у часов - буквы цвета текста панели задач на прозрачном фоне, как
// у Windows, или они же в рамке (контур скруглённого прямоугольника, как у значков Windows у часов); флажок у
// текстового курсора - буквы на тёмной плашке со светлой каймой, чтобы их было видно на любом фоне. Рисует
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

// Наборы в настройке flagsSet: буквы и буквы в рамке.
inline constexpr const char* kPlain = "Letters";
inline constexpr const char* kFramed = "LettersFramed";

inline bool Is(const std::string& set) { return set == kPlain || set == kFramed; }

enum class Style { Plain, Frame, Badge };

// "en-US" -> "EN".
inline std::wstring Text(const std::wstring& locale) {
	std::wstring lang = locale.substr(0, locale.find(L'-'));
	for (auto& c : lang) c = (wchar_t)(UINT_PTR)CharUpperW((LPWSTR)(UINT_PTR)c);
	return lang;
}

// Цвет языка - для черты под буквами в рамке, с его флага: английский - синий (США, Британия), русский - красный,
// украинский - жёлтый и т. д.; цвет, который уже занят, - соседний оттенок. Незнакомый язык - из запасных по коду.
// Цвета - средней яркости: видны и на тёмной панели задач, и на светлой. 0xRRGGBB.
inline UINT32 Accent(const std::wstring& text) {
	static const std::pair<const wchar_t*, UINT32> known[] = {
		{ L"EN", 0x3B82F6 }, // синий
		{ L"RU", 0xEF4444 }, // красный - нижняя полоса
		{ L"UK", 0xFACC15 }, // жёлтый
		{ L"BE", 0x22C55E }, // зелёный
		{ L"KK", 0x06B6D4 }, // голубой
		{ L"DE", 0xF59E0B }, // золотой
		{ L"FR", 0x6366F1 }, // синий потемнее (синий - у английского)
		{ L"ES", 0xF97316 }, // оранжевый - красный с жёлтым
		{ L"IT", 0x10B981 }, // зелёный потемнее
		{ L"PL", 0xEC4899 }, // розовый - белый с красным
		{ L"TR", 0xDC2626 }, // красный потемнее
		{ L"HY", 0xFB923C }, // оранжевый посветлее - нижняя полоса
		{ L"KA", 0xF43F5E }, // малиновый - кресты
		{ L"AZ", 0x0EA5E9 }, // голубой посветлее
		{ L"UZ", 0x38BDF8 }, // небесный
	};
	for (const auto& [code, color] : known)
		if (text == code) return color;
	static const UINT32 spare[] = { 0xA855F7, 0x14B8A6, 0x84CC16, 0xE879F9, 0x22D3EE, 0xFB7185 };
	unsigned hash = 0;
	for (wchar_t c : text) hash = hash * 31 + c;
	return spare[hash % std::size(spare)];
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

// Буквы в картинке w x h. Plain и Frame - значок у часов, цветом текста панели задач: dark - тёмная панель (белые).
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

	// Рамка: во всю ширину, высотой в семь восьмых, ровно посередине (поля сверху и снизу равны); толщина - точка
	// (на 200 % - две). Внутри - буквы и под ними черта цвета языка (Accent). Плашка у курсора - вся картинка.
	// (Было: рамка в три четверти высоты и поля шире, без черты; Дмитрий 06.10: "иконки немного побольше" и
	// "подчеркнуть черточкой разного цвета".)
	const bool frame = style == Style::Frame;
	const float stroke = frame ? (float)(std::max)(1, (int)std::floor(h / 16.0f + 0.25f)) : 1.0f;
	float frameTop = 0, frameBottom = (float)h;
	if (frame) {
		int fh = (int)std::lround(h * 0.875f);
		if ((h - fh) % 2) fh++;
		frameTop = (h - fh) / 2.0f;
		frameBottom = frameTop + fh;
	}
	const float frameH = frameBottom - frameTop;
	const int barH = (int)stroke, barGap = (std::max)(1, (int)std::lround(h / 16.0f));

	// Кегль: заглавные не выше 62 % высоты (в рамке - внутренней части без черты, на плашке - 58 %), и буквы не шире
	// места.
	const DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
	const float capShare = details::CapHeight(weight);
	const float inner = style == Style::Plain ? (float)h
		: frame ? frameH - 2 * stroke - barH - barGap
		: frameH - 2 * stroke;
	const float padX = style == Style::Plain ? 0
		: frame ? stroke + (std::max)(1.0f, std::round(h * 0.09f))
		: stroke + (std::max)(1.0f, std::round(h * 0.14f));
	float size = inner * (style == Style::Badge ? 0.58f : frame ? 0.72f : 0.62f) / capShare;
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
		D2D1_RECT_F bar{}; // черта под буквами в рамке: где - после того, как встали буквы
		bool hasBar = false;
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
			if (shape && style == Style::Frame) {
				const float half = stroke / 2, r = frameH * 0.3f;
				D2D1_COLOR_F edge = ink;
				edge.a = alpha;
				brush->SetColor(edge);
				rt->DrawRoundedRectangle({ { half, frameTop + half, w - half, frameBottom - half }, r - half, r - half },
				                         brush.Get(), stroke);
			}
			D2D1_COLOR_F letters = ink;
			letters.a = alpha;
			brush->SetColor(letters);
			rt->DrawTextLayout({ x, y }, layout.Get(), brush.Get());
			if (shape && hasBar) {
				brush->SetColor(D2D1::ColorF(Accent(text), alpha));
				const float r = (bar.bottom - bar.top) / 2;
				rt->FillRoundedRectangle({ bar, r, r }, brush.Get());
			}
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
				const int inkW = right - left + 1, inkH = bottom - top + 1, newLeft = (w - inkW) / 2;
				x += (float)(newLeft - left);
				if (frame) {
					// Буквы с чертой под ними - посередине рамки; черта - в две трети ширины букв, посередине под ними.
					const int innerTop = (int)(frameTop + stroke), innerH = (int)(frameH - 2 * stroke);
					const int blockTop = innerTop + (innerH - (inkH + barGap + barH)) / 2;
					y += (float)(blockTop - top);
					int barW = (std::max)(3, (int)std::lround(inkW * 0.66));
					if ((inkW - barW) % 2) barW++;
					const float barLeft = (float)(newLeft + (inkW - barW) / 2), barTop = (float)(blockTop + inkH + barGap);
					bar = { barLeft, barTop, barLeft + barW, barTop + barH };
					hasBar = true;
				}
				else
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
