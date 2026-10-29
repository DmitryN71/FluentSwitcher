#pragma once

// Стрелка CapsLock у флага (flag_capslock): пока включён CapsLock - красная стрелка вверх, у текстового курсора справа от
// флага, на указателе мыши - слева от палочки (справа от неё - флаг, места нет; MouseCursor.h). Форум: полоса над флагом
// "слишком абстрактно" (AlexPORTrb, 29.10.2026), голосованием - красная (3 на https://dmitryn71.github.io/FluentSwitcher/caps/),
// формой - 6: толстая, как стрелка на клавише Shift (Дмитрий, 29.10.2026). Сглажена, как буквы на плашке (Direct2D); флаг
// и плашка не тронуты, по высоте ничего не сдвигается. Только поток интерфейса движка.

#include "LetterIcons.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace CapsArrow {

inline constexpr uint32_t kRed = 0xd13438;

// Для флага с непрозрачной частью высотой body: зазор до стрелки и её высота.
inline int Gap(int body) { return (std::max)(1, (int)std::lround(body * 0.12)); }
inline int Height(int body) { return (std::max)(7, (int)std::lround(body * 0.9)); }

// Стрелка высотой h, её ширина - w: RGBA без умножения на альфу. Ширина - 0,8 высоты, ствол - 0,55 ширины, наконечник -
// 0,45 высоты, углы острые; ствол - на целых точках (ширина и ствол одной чётности), края его чёткие.
inline std::vector<unsigned char> Draw(int h, int& w) {
	using Microsoft::WRL::ComPtr;
	const int stem = (std::max)(2, (int)std::lround(h * 0.8 * 0.55));
	w = (std::max)(stem + 4, (int)std::lround(h * 0.8));
	if ((w - stem) % 2) w++;
	std::vector<unsigned char> rgba((size_t)w * h * 4, 0);
	auto& d2d = LetterIcons::details::D2D();
	if (!d2d || h <= 0) return rgba;

	BITMAPINFO bi{};
	bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
	void* bits = nullptr;
	HDC screen = GetDC(nullptr);
	HDC mem = CreateCompatibleDC(screen);
	HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	ReleaseDC(nullptr, screen);
	if (!bmp) {
		DeleteDC(mem);
		return rgba;
	}
	HGDIOBJ old = SelectObject(mem, bmp);
	// Процессором, как буквы (LetterIcons.h).
	const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
	ComPtr<ID2D1DCRenderTarget> rt;
	ComPtr<ID2D1SolidColorBrush> brush;
	ComPtr<ID2D1PathGeometry> shape;
	ComPtr<ID2D1GeometrySink> sink;
	RECT rc{ 0, 0, w, h };
	if (SUCCEEDED(d2d->CreateDCRenderTarget(&props, &rt)) && SUCCEEDED(rt->BindDC(mem, &rc)) &&
		SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(kRed), &brush)) && SUCCEEDED(d2d->CreatePathGeometry(&shape)) &&
		SUCCEEDED(shape->Open(&sink))) {
		const float W = (float)w, H = (float)h, head = (float)std::lround(h * 0.45), cx = W / 2;
		const float left = (W - stem) / 2, right = (W + stem) / 2;
		sink->BeginFigure({ cx, 0 }, D2D1_FIGURE_BEGIN_FILLED);
		const D2D1_POINT_2F points[] = { { W, head }, { right, head }, { right, H }, { left, H }, { left, head }, { 0, head } };
		sink->AddLines(points, (UINT32)std::size(points));
		sink->EndFigure(D2D1_FIGURE_END_CLOSED);
		if (SUCCEEDED(sink->Close())) {
			rt->BeginDraw();
			rt->Clear(D2D1::ColorF(0, 0, 0, 0));
			rt->FillGeometry(shape.Get(), brush.Get());
			if (SUCCEEDED(rt->EndDraw())) {
				GdiFlush();
				const auto* px = (const unsigned char*)bits;
				for (int i = 0; i < w * h; i++) {
					const unsigned a = px[i * 4 + 3];
					auto un = [a](unsigned c) { return (unsigned char)(a ? (std::min)(255u, c * 255 / a) : 0); };
					rgba[i * 4 + 0] = un(px[i * 4 + 2]);
					rgba[i * 4 + 1] = un(px[i * 4 + 1]);
					rgba[i * 4 + 2] = un(px[i * 4 + 0]);
					rgba[i * 4 + 3] = (unsigned char)a;
				}
			}
		}
	}
	SelectObject(mem, old);
	DeleteObject(bmp);
	DeleteDC(mem);
	return rgba;
}

// Флажок у текстового курсора: картинка (RGBA без умножения на альфу, w x h) шире вправо - за флагом зазор и стрелка,
// посередине его непрозрачной части по высоте. Флажок привязан к каретке левым краем (CaretFlag.h) - флаг не двигается.
inline void Beside(std::vector<unsigned char>& rgba, int& w, int h) {
	if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * h * 4) return;
	int top = h, bottom = -1, right = -1;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			if (rgba[((size_t)y * w + x) * 4 + 3] > 200) {
				top = (std::min)(top, y);
				bottom = (std::max)(bottom, y);
				right = (std::max)(right, x);
			}
	if (bottom < 0) return;
	const int body = bottom - top + 1, ah = (std::min)(h, Height(body));
	int aw = 0;
	const auto arrow = Draw(ah, aw);
	const int ax = right + 1 + Gap(body), ay = (std::max)(0, top + (body - ah) / 2);
	const int nw = (std::max)(w, ax + aw);
	std::vector<unsigned char> out((size_t)nw * h * 4, 0);
	for (int y = 0; y < h; y++)
		std::copy(rgba.begin() + (size_t)y * w * 4, rgba.begin() + (size_t)(y + 1) * w * 4, out.begin() + (size_t)y * nw * 4);
	// Поверх того, что там есть (тень флага с переливом).
	for (int y = 0; y < ah && ay + y < h; y++)
		for (int x = 0; x < aw; x++) {
			const unsigned char* s = &arrow[((size_t)y * aw + x) * 4];
			if (!s[3]) continue;
			unsigned char* d = &out[((size_t)(ay + y) * nw + ax + x) * 4];
			const double a = s[3] / 255.0, da = d[3] / 255.0, ra = a + da * (1 - a);
			for (int c = 0; c < 3; c++) d[c] = (unsigned char)std::lround((s[c] * a + d[c] * da * (1 - a)) / ra);
			d[3] = (unsigned char)std::lround(ra * 255);
		}
	rgba.swap(out);
	w = nw;
}

}
