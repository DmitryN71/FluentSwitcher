// Указатель-текст с флагом раскладки (MouseFlag.h) - только картинки и Windows, без настроек движка: его проверяет и
// tools\test_mousecursor.cmd.
#pragma once

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace MouseCursor {

// Указатель картинкой.
struct Picture {
	int w = 0, h = 0, hotX = 0, hotY = 0;
	bool mono = false;            // одноцветный (маска и XOR - инвертирует фон)
	bool alpha = false;           // цветной с прозрачностью; иначе Windows рисует по маске
	std::vector<uint32_t> color;  // BGRA построчно
	std::vector<uint8_t> andMask; // 1 - пиксель экрана остаётся (а с цветом - инвертируется)
};

// Указатель cursor картинкой. false - не вышло.
inline bool Capture(HCURSOR cursor, Picture& out) {
	out = {};
	ICONINFO ii{};
	if (!cursor || !GetIconInfo(cursor, &ii)) return false;
	BITMAP bm{};
	GetObjectW(ii.hbmMask, sizeof(bm), &bm);
	const bool mono = !ii.hbmColor;
	const int w = bm.bmWidth, h = mono ? bm.bmHeight / 2 : bm.bmHeight;
	HDC dc = GetDC(nullptr);
	auto read = [&](HBITMAP b, int rows) {
		std::vector<uint32_t> px((size_t)(std::max)(w, 0) * (std::max)(rows, 0));
		BITMAPINFO bi{};
		bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -rows, 1, 32, BI_RGB };
		if (px.empty() || !GetDIBits(dc, b, 0, rows, px.data(), &bi, DIB_RGB_COLORS)) px.clear();
		return px;
	};
	const auto mask = read(ii.hbmMask, mono ? 2 * h : h);
	std::vector<uint32_t> color;
	if (!mono) color = read(ii.hbmColor, h);
	ReleaseDC(nullptr, dc);
	DeleteObject(ii.hbmMask);
	if (ii.hbmColor) DeleteObject(ii.hbmColor);
	if (w <= 0 || h <= 0 || mask.empty() || (!mono && color.empty())) return false;
	out.w = w;
	out.h = h;
	out.hotX = ii.xHotspot;
	out.hotY = ii.yHotspot;
	out.mono = mono;
	const size_t n = (size_t)w * h;
	out.andMask.resize(n);
	out.color.resize(n);
	for (size_t i = 0; i < n; i++) {
		out.andMask[i] = (mask[i] & 0xFFFFFF) != 0;
		// Одноцветный: нижняя половина маски - XOR (1 там, где пиксель белый или инвертирует фон).
		out.color[i] = mono ? ((mask[n + i] & 0xFFFFFF) ? 0x00FFFFFFu : 0u) : color[i];
		if (!mono && (color[i] >> 24)) out.alpha = true;
	}
	return true;
}

// Картинка RGBA (rgba, sw x sh) - в size x size, усреднением с весом прозрачности. 0xAARRGGBB.
inline std::vector<uint32_t> Scaled(const unsigned char* rgba, int sw, int sh, int size) {
	std::vector<uint32_t> out((size_t)size * size);
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			const int x0 = x * sw / size, x1 = (std::max)(x0 + 1, (x + 1) * sw / size);
			const int y0 = y * sh / size, y1 = (std::max)(y0 + 1, (y + 1) * sh / size);
			double r = 0, g = 0, b = 0, a = 0;
			int n = 0;
			for (int yy = y0; yy < y1 && yy < sh; yy++)
				for (int xx = x0; xx < x1 && xx < sw; xx++, n++) {
					const unsigned char* p = rgba + ((size_t)yy * sw + xx) * 4;
					const double pa = p[3] / 255.0;
					r += p[0] * pa;
					g += p[1] * pa;
					b += p[2] * pa;
					a += pa;
				}
			if (!n || a <= 0) continue;
			const uint32_t A = (uint32_t)std::lround(a / n * 255);
			out[(size_t)y * size + x] = (A << 24) | ((uint32_t)std::lround(r / a) << 16) |
				((uint32_t)std::lround(g / a) << 8) | (uint32_t)std::lround(b / a);
		}
	}
	return out;
}

// Указатель - в цвет с прозрачностью (ARGB), как стрелка Windows. Указатель с маской (палочка Windows - одноцветная и
// инвертирует фон под собой) Windows рисует с цветными точками не везде верно: флаг на нём был кашей (Дмитрий,
// 16.10.2026, 1.5.14-test1: "американский можно различить, российский - прямоугольник, с переливом и буквы - mojibake").
// Инвертирующие точки - чёрные, вокруг них - белая кайма: видно и на светлом, и на тёмном; чёрные и белые - как были.
inline Picture Argb(const Picture& src) {
	if (src.alpha) return src;
	Picture p = src;
	const int w = p.w, h = p.h;
	const size_t n = (size_t)w * h;
	std::vector<uint8_t> invert(n);
	for (size_t i = 0; i < n; i++) {
		const uint32_t c = src.color[i] & 0xFFFFFF;
		if (!src.andMask[i]) p.color[i] = 0xFF000000 | c; // своя точка: чёрная, белая, цветная
		else if (c) {
			p.color[i] = 0xFF000000; // инвертирующая - чёрной
			invert[i] = 1;
		}
		else p.color[i] = 0; // прозрачная
	}
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			const size_t i = (size_t)y * w + x;
			if (p.color[i] >> 24) continue;
			bool edge = false;
			for (int dy = -1; dy <= 1 && !edge; dy++)
				for (int dx = -1; dx <= 1 && !edge; dx++) {
					const int X = x + dx, Y = y + dy;
					edge = X >= 0 && Y >= 0 && X < w && Y < h && invert[(size_t)Y * w + X];
				}
			if (edge) p.color[i] = 0xFFFFFFFF;
		}
	for (size_t i = 0; i < n; i++) p.andMask[i] = (p.color[i] >> 24) == 0;
	p.mono = false;
	p.alpha = true;
	return p;
}

// Где на картинке указателя нарисовано что-то (включительно): палочка у указателя Windows - не посередине, на 32 точках -
// слева сверху (горячая точка 8, 9).
struct Box {
	int left = 0, top = 0, right = -1, bottom = -1;
};
inline Box Drawn(const Picture& p) {
	Box b{ p.w, p.h, -1, -1 };
	for (int y = 0; y < p.h; y++)
		for (int x = 0; x < p.w; x++) {
			const size_t i = (size_t)y * p.w + x;
			const bool seen = p.alpha ? (p.color[i] >> 24) != 0 : !(p.andMask[i] && (p.color[i] & 0xFFFFFF) == 0);
			if (!seen) continue;
			b.left = (std::min)(b.left, x);
			b.top = (std::min)(b.top, y);
			b.right = (std::max)(b.right, x);
			b.bottom = (std::max)(b.bottom, y);
		}
	return b;
}

// Флаг - вплотную справа от нарисованного, низом к его низу (нижней перекладине палочки).
inline int FlagLeft(const Picture& p) { return Drawn(p).right + 2; }
// Сторона квадрата флага: девять десятых высоты палочки, кратно 4 (картинки флагов - 16, 20, 24... точек: без
// уменьшения флаг не мылится), не шире места справа; меньше 12 - сколько есть.
inline int FlagSize(const Picture& p) {
	const Box b = Drawn(p);
	if (b.right < 0) return 0;
	const int room = p.w - (b.right + 2);
	const int want = (int)std::lround((b.bottom - b.top + 1) * 0.9 / 4.0) * 4;
	const int size = (std::min)(want, room / 4 * 4);
	return size >= 12 ? size : (std::min)(room, 12);
}

// Указатель base (ARGB - Argb) с флагом flag (квадрат size x size, 0xAARRGGBB; флаг в нём - видимой частью) справа от
// палочки, у её нижней перекладины. nullptr - не вышло.
inline HCURSOR Compose(const Picture& base, const std::vector<uint32_t>& flag, int size) {
	const int w = base.w, h = base.h;
	if (!base.alpha) return nullptr;
	if (w <= 0 || h <= 0 || size <= 0 || flag.size() != (size_t)size * size) return nullptr;
	int top = size, bottom = -1;
	for (int y = 0; y < size; y++)
		for (int x = 0; x < size; x++)
			if ((flag[(size_t)y * size + x] >> 24) > 40) {
				top = (std::min)(top, y);
				bottom = (std::max)(bottom, y);
			}
	if (bottom < 0) return nullptr;
	const Box drawn = Drawn(base);
	if (drawn.right < 0) return nullptr;
	const int fx = drawn.right + 2;
	const int fy = drawn.bottom - bottom; // низ видимого флага - у низа палочки
	auto color = base.color;
	auto andMask = base.andMask;
	for (int y = top; y <= bottom; y++) {
		for (int x = 0; x < size; x++) {
			const int X = fx + x, Y = fy + y;
			if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
			const uint32_t s = flag[(size_t)y * size + x];
			const uint32_t sa = s >> 24;
			const size_t i = (size_t)Y * w + X;
			// Прозрачность указателя - обычная (не умноженная на цвет): смешать.
			const uint32_t d = color[i], da = d >> 24;
			const uint32_t oa = sa + da * (255 - sa) / 255;
			if (!oa) continue;
			auto mix = [&](int shift) {
				const uint32_t sc = (s >> shift) & 0xFF, dc = (d >> shift) & 0xFF;
				return ((sc * sa + dc * da * (255 - sa) / 255) / oa) << shift;
			};
			color[i] = (oa << 24) | mix(16) | mix(8) | mix(0);
			andMask[i] = 0;
		}
	}
	HDC dc = GetDC(nullptr);
	BITMAPINFO bi{};
	bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
	void* bits = nullptr;
	HBITMAP hColor = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	ReleaseDC(nullptr, dc);
	if (!hColor) return nullptr;
	memcpy(bits, color.data(), color.size() * 4);
	const int stride = (w + 15) / 16 * 2; // строки маски - по 16 бит
	std::vector<uint8_t> maskBits((size_t)stride * h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			if (andMask[(size_t)y * w + x]) maskBits[(size_t)y * stride + x / 8] |= (uint8_t)(0x80 >> (x % 8));
	HBITMAP hMask = CreateBitmap(w, h, 1, 1, maskBits.data());
	ICONINFO ii{ FALSE, (DWORD)base.hotX, (DWORD)base.hotY, hMask, hColor };
	HCURSOR cursor = hMask ? (HCURSOR)CreateIconIndirect(&ii) : nullptr;
	if (hMask) DeleteObject(hMask);
	DeleteObject(hColor);
	return cursor;
}

}
