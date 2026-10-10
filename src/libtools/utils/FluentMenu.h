#pragma once

// Меню у флага в стиле Windows 11 - своё окно вместо меню Windows (TrackPopupMenu). Обычное меню Windows рисует
// сама: плотные строки, галочка, без значков, и этого не изменить. Здесь - как у меню самой Windows 11 у часов
// (сеть, звук) и у окна настроек: скругления, рамка и тень от Windows (DWM), строки по 30 точек со скруглённой
// подсветкой, значки из системного шрифта (Segoe Fluent Icons, на Windows 10 - Segoe MDL2 Assets), переключатель
// вместо галочки, сверху - название и версия. Тема - как у панели задач, размеры - по масштабу монитора; рисует
// Direct2D / DirectWrite. Мышь, стрелки, Enter, Esc, первая буква пункта; переключатель меню не закрывает, остальное
// закрывает; щелчок мимо, уход в другое окно, клавиша Win - тоже.
// Не вышло (контрастная тема, нет Direct2D) - Show возвращает false, и WinTray показывает обычное меню.
// Всё - в потоке интерфейса движка (окно значка у часов). View рисует и без окна: tools\test_menu снимает картинки.

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

class FluentMenu {
public:
	struct Item {
		std::wstring text;
		wchar_t icon = 0;      // знак шрифта значков, 0 - без значка
		bool separator = false;
		bool toggle = false;   // переключатель справа (вместо галочки); щелчок по нему меню не закрывает
		bool on = false;
		std::function<void()> action;
		std::function<bool()> state; // переключатель после action: включён ли на самом деле (нет - просто наоборот)
	};

	// Одно меню: пункты, тема, размеры (в DIP - 1/96 дюйма) и рисование.
	struct View {
		std::wstring title;
		std::vector<Item> items;
		bool dark = true;
		bool win11 = true;
		D2D1_COLOR_F accent{};
		int hover = -1;   // подсвеченный пункт (мышь или стрелки)
		int pressed = -1; // нажатый мышью
		float width = 0, height = 0;
		std::vector<D2D1_RECT_F> rects; // по пункту
		D2D1_RECT_F titleRect{};

		// Как меню Windows 11 у часов (сеть, звук): строки 30, без лишней ширины.
		static constexpr float kPad = 4, kTitle = 26, kItem = 30, kSeparator = 7, kInset = 12, kIcon = 16, kGap = 12,
		                       kToggleW = 36, kToggleH = 18, kToggleGap = 16, kKnob = 5, kMinWidth = 160, kRadius = 4;

		bool Selectable(int i) const { return i >= 0 && i < (int)items.size() && !items[i].separator; }

		bool Layout(IDWriteFactory* dw) {
			using Microsoft::WRL::ComPtr;
			if (!MakeFormats(dw)) return false;
			auto measure = [dw](const std::wstring& s, IDWriteTextFormat* f) {
				ComPtr<IDWriteTextLayout> layout;
				DWRITE_TEXT_METRICS m{};
				if (s.empty() || FAILED(dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 10000, 100, &layout)) ||
					FAILED(layout->GetMetrics(&m)))
					return 0.0f;
				return m.widthIncludingTrailingWhitespace;
			};
			float text = 0;
			bool toggles = false;
			for (const auto& it : items) {
				if (it.separator) continue;
				text = (std::max)(text, measure(it.text, m_text.Get()));
				toggles = toggles || it.toggle;
			}
			width = kPad + kInset + kIcon + kGap + text + (toggles ? kToggleGap + kToggleW : 0) + kInset + kPad;
			width = (std::max)(width, kPad + kInset + measure(title, m_small.Get()) + kInset + kPad);
			width = std::ceil((std::max)(width, kMinWidth));
			float y = kPad;
			if (!title.empty()) {
				titleRect = { kPad + kInset, y, width - kPad - kInset, y + kTitle };
				y += kTitle;
			}
			rects.clear();
			for (const auto& it : items) {
				const float h = it.separator ? kSeparator : kItem;
				rects.push_back({ kPad, y, width - kPad, y + h });
				y += h;
			}
			height = y + kPad;
			return true;
		}

		// Пункт под точкой (DIP), -1 - нет.
		int HitTest(float x, float y) const {
			for (int i = 0; i < (int)rects.size(); i++) {
				const auto& r = rects[i];
				if (Selectable(i) && x >= r.left && x < r.right && y >= r.top && y < r.bottom) return i;
			}
			return -1;
		}

		// rt - в DIP (dpi рисовальщика = масштаб монитора); pixel - сколько DIP в одной точке экрана.
		void Paint(ID2D1RenderTarget* rt, float pixel) {
			using Microsoft::WRL::ComPtr;
			const auto c = [](UINT32 rgb, float a = 1) { return D2D1::ColorF(rgb, a); };
			const D2D1_COLOR_F bg = dark ? c(0x2c2c2c) : c(0xf9f9f9);
			const D2D1_COLOR_F text = dark ? c(0xffffff) : c(0x1b1b1b);
			const D2D1_COLOR_F text2 = dark ? c(0xa8a8a8) : c(0x616161);
			const D2D1_COLOR_F hoverFill = dark ? c(0xffffff, 0.0605f) : c(0x000000, 0.0373f);
			const D2D1_COLOR_F pressFill = dark ? c(0xffffff, 0.0419f) : c(0x000000, 0.0241f);
			const D2D1_COLOR_F line = dark ? c(0xffffff, 0.0837f) : c(0x000000, 0.0803f);
			const D2D1_COLOR_F onAccent = dark ? c(0x000000) : c(0xffffff);
			ComPtr<ID2D1SolidColorBrush> brush;
			if (FAILED(rt->CreateSolidColorBrush(text, &brush))) return;
			auto fill = [&](const D2D1_COLOR_F& col) {
				brush->SetColor(col);
				return brush.Get();
			};
			// Ровно по точкам экрана (линии 1 точка, без размытия на 125 и 150 %).
			auto snap = [pixel](float v) { return std::round(v / pixel) * pixel; };

			rt->Clear(bg);
			if (!win11) { // на Windows 10 рамку меню DWM не рисует
				rt->DrawRectangle({ pixel / 2, pixel / 2, width - pixel / 2, height - pixel / 2 },
				                  fill(dark ? c(0x3d3d3d) : c(0xd9d9d9)), pixel);
			}
			if (!title.empty())
				rt->DrawText(title.c_str(), (UINT32)title.size(), m_small.Get(), titleRect, fill(text2));
			for (int i = 0; i < (int)items.size(); i++) {
				const auto& it = items[i];
				const auto& r = rects[i];
				if (it.separator) {
					const float top = snap((r.top + r.bottom) / 2);
					rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
					rt->FillRectangle({ r.left, top, r.right, top + pixel }, fill(line));
					rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
					continue;
				}
				if (i == pressed || i == hover)
					rt->FillRoundedRectangle({ r, kRadius, kRadius }, fill(i == pressed ? pressFill : hoverFill));
				float x = r.left + kInset;
				if (it.icon)
					rt->DrawText(&it.icon, 1, m_icon.Get(), { x, r.top, x + kIcon, r.bottom }, fill(text));
				x += kIcon + kGap;
				const float right = it.toggle ? r.right - kInset - kToggleW - kToggleGap / 2 : r.right - kInset;
				rt->DrawText(it.text.c_str(), (UINT32)it.text.size(), m_text.Get(), { x, r.top, right, r.bottom },
				             fill(text), D2D1_DRAW_TEXT_OPTIONS_CLIP);
				if (it.toggle) {
					const float cy = (r.top + r.bottom) / 2, tr = r.right - kInset, tl = tr - kToggleW;
					const float h = kToggleH / 2;
					if (it.on) {
						rt->FillRoundedRectangle({ { tl, cy - h, tr, cy + h }, h, h }, fill(accent));
						rt->FillEllipse(D2D1::Ellipse({ tr - h, cy }, kKnob, kKnob), fill(onAccent));
					}
					else {
						const float w = pixel;
						rt->DrawRoundedRectangle({ { tl + w / 2, cy - h + w / 2, tr - w / 2, cy + h - w / 2 }, h - w / 2, h - w / 2 },
						                         fill(text2), w);
						rt->FillEllipse(D2D1::Ellipse({ tl + h, cy }, kKnob, kKnob), fill(text2));
					}
				}
			}
		}

	private:
		Microsoft::WRL::ComPtr<IDWriteTextFormat> m_text, m_small, m_icon;

		static std::wstring Family(IDWriteFactory* dw, std::initializer_list<const wchar_t*> names) {
			Microsoft::WRL::ComPtr<IDWriteFontCollection> fonts;
			if (SUCCEEDED(dw->GetSystemFontCollection(&fonts))) {
				for (auto n : names) {
					UINT32 index = 0;
					BOOL exists = FALSE;
					if (SUCCEEDED(fonts->FindFamilyName(n, &index, &exists)) && exists) return n;
				}
			}
			return *(names.end() - 1);
		}

		bool MakeFormats(IDWriteFactory* dw) {
			const std::wstring ui = Family(dw, { L"Segoe UI Variable Text", L"Segoe UI" });
			const std::wstring icons = Family(dw, { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" });
			auto make = [dw](const std::wstring& family, float size, Microsoft::WRL::ComPtr<IDWriteTextFormat>& out) {
				if (FAILED(dw->CreateTextFormat(family.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
				                                DWRITE_FONT_STRETCH_NORMAL, size, L"", &out)))
					return false;
				out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
				out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
				return true;
			};
			if (!make(ui, 14, m_text) || !make(ui, 12, m_small) || !make(icons, 16, m_icon)) return false;
			m_icon->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			return true;
		}
	};

	// Открыть у точки pt (точки экрана): низ меню у курсора, как TrackPopupMenu с TPM_BOTTOMALIGN; не влезает -
	// внутрь рабочей области монитора. false - не вышло, нужно обычное меню.
	static bool Show(std::wstring title, std::vector<Item> items, POINT pt) {
		Close();
		if (HighContrast() || !Factories()) return false;
		s_view = View{};
		s_view.title = std::move(title);
		s_view.items = std::move(items);
		s_view.dark = TaskbarDark();
		s_view.win11 = Build() >= 22000;
		s_view.accent = Accent(s_view.dark, s_view.win11);

		static const wchar_t* cls = [] {
			WNDCLASSEXW wc{ sizeof(wc) };
			wc.style = CS_DROPSHADOW;
			wc.lpfnWndProc = WndProc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wc.lpszClassName = L"FluentSwitcher_FluentMenu";
			RegisterClassExW(&wc);
			return L"FluentSwitcher_FluentMenu";
		}();
		// Сначала крошечное и невидимое там, где откроется: так известен масштаб того монитора.
		HWND wnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, cls, L"FluentSwitcher", WS_POPUP, pt.x, pt.y, 1, 1,
		                           nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
		if (!wnd) return false;
		s_wnd = wnd;
		s_dpi = GetDpiForWindow(wnd);
		if (!s_dpi) s_dpi = 96;
		if (!s_view.Layout(s_dw.Get())) {
			Close();
			return false;
		}
		const float scale = s_dpi / 96.0f;
		const int w = (int)std::ceil(s_view.width * scale), h = (int)std::ceil(s_view.height * scale);
		MONITORINFO mi{ sizeof(mi) };
		GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
		const RECT work = mi.rcWork;
		const int gap = (int)std::round(8 * scale); // от панели задач, как у меню самой Windows 11
		int x = pt.x, y = pt.y - h;
		if (x + w > work.right) x = pt.x - w;
		if (y < work.top) y = pt.y;
		x = std::clamp(x, (int)work.left, (int)(std::max)(work.left, work.right - w));
		if (y + h > work.bottom) y = work.bottom - h - gap;
		if (y < work.top) y = work.top + (pt.y < work.top ? gap : 0);

		const DWORD round = 2; // DWMWCP_ROUND; Windows 10 не знает - углы прямые
		DwmSetWindowAttribute(wnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &round, sizeof(round));
		const BOOL dark = s_view.dark;
		DwmSetWindowAttribute(wnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
		const COLORREF border = s_view.dark ? RGB(0x3d, 0x3d, 0x3d) : RGB(0xe0, 0xe0, 0xe0);
		DwmSetWindowAttribute(wnd, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));

		SetWindowPos(wnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
		ShowWindow(wnd, SW_SHOW);
		SetForegroundWindow(wnd);
		s_fgAtShow = GetForegroundWindow();
		// Кнопка, которой меню открыли (правая - на нажатии), ещё нажата: щелчком мимо она станет, только когда её отпустят.
		s_buttonUp[0] = !(GetAsyncKeyState(VK_LBUTTON) & 0x8000);
		s_buttonUp[1] = !(GetAsyncKeyState(VK_RBUTTON) & 0x8000);
		SetTimer(wnd, 1, 50, nullptr);
		return true;
	}

	static void Close() {
		if (s_wnd) DestroyWindow(s_wnd);
	}

	static bool IsOpen() { return s_wnd != nullptr; }

	// Тема панели задач (не программ): меню у флага - часть её, как у Windows.
	static bool TaskbarDark() {
		DWORD light = 1, size = sizeof(light);
		RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
		             L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
		return !light;
	}

	static DWORD Build() {
		static DWORD build = [] {
			using RtlGetVersion_t = LONG(WINAPI*)(OSVERSIONINFOW*);
			auto fn = (RtlGetVersion_t)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
			OSVERSIONINFOW v{ sizeof(v) };
			return fn && fn(&v) == 0 ? v.dwBuildNumber : 0;
		}();
		return build;
	}

	// Цвет акцента Windows, как в окне настроек (fluent_ui.cpp, WindowsAccent): на Windows 11 - светлее на тёмном
	// и темнее на светлом (палитра, которую Windows держит для программ), на Windows 10 - сам цвет.
	static D2D1_COLOR_F Accent(bool dark, bool win11) {
		BYTE palette[32] = {};
		DWORD size = sizeof(palette);
		if (RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Explorer\Accent)", L"AccentPalette",
		                 RRF_RT_REG_BINARY, nullptr, palette, &size) == ERROR_SUCCESS && size >= 32) {
			const BYTE* p = palette + (!win11 ? 3 : dark ? 1 : 4) * 4;
			return D2D1::ColorF(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f);
		}
		return D2D1::ColorF(dark ? 0x60cdff : 0x005fb8);
	}

	static bool Factories() {
		if (!s_d2d && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, s_d2d.ReleaseAndGetAddressOf())))
			return false;
		if (!s_dw && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
		                                        (IUnknown**)s_dw.ReleaseAndGetAddressOf())))
			return false;
		return true;
	}

	static IDWriteFactory* DWrite() { return s_dw.Get(); }
	static ID2D1Factory* D2D() { return s_d2d.Get(); }

private:
	static inline HWND s_wnd = nullptr;
	static inline View s_view;
	static inline UINT s_dpi = 96;
	static inline HWND s_fgAtShow = nullptr;
	static inline bool s_buttonUp[2] = {};
	static inline bool s_tracking = false;
	static inline Microsoft::WRL::ComPtr<ID2D1Factory> s_d2d;
	static inline Microsoft::WRL::ComPtr<IDWriteFactory> s_dw;
	static inline Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> s_rt;

	static bool HighContrast() {
		HIGHCONTRASTW hc{ sizeof(hc) };
		return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
	}

	static int Hit(LPARAM lp) {
		const float k = 96.0f / s_dpi;
		return s_view.HitTest((short)LOWORD(lp) * k, (short)HIWORD(lp) * k);
	}

	static void SetHover(int i) {
		if (i == s_view.hover) return;
		s_view.hover = i;
		InvalidateRect(s_wnd, nullptr, FALSE);
	}

	// Стрелки: следующий пункт (мимо разделителей), по кругу.
	static void Move(int step) {
		const int n = (int)s_view.items.size();
		int i = s_view.hover;
		if (i < 0) i = step > 0 ? -1 : n;
		for (int k = 0; k < n; k++) {
			i = ((i + step) % n + n) % n;
			if (s_view.Selectable(i)) return SetHover(i);
		}
	}

	// Пункт выбран: меню закрыть, потом действие ("Выход" завершит программу, когда окна уже нет). Переключатель -
	// переключить и оставить меню открытым, как в быстрых настройках Windows.
	static void Choose(int i) {
		if (!s_view.Selectable(i)) return;
		auto& item = s_view.items[i];
		if (item.toggle) {
			if (item.action) item.action();
			item.on = item.state ? item.state() : !item.on;
			InvalidateRect(s_wnd, nullptr, FALSE);
			return;
		}
		auto action = item.action;
		Close();
		if (action) action();
	}

	static void Paint(HWND wnd) {
		PAINTSTRUCT ps;
		BeginPaint(wnd, &ps);
		if (!s_rt) {
			RECT rc{};
			GetClientRect(wnd, &rc);
			// Рисует процессор, не видеокарта: на видеокарте первое же меню стоило движку 37 МБ (драйвер), и половина их
			// оставалась до выхода - "не освобождает память, поднимет раза в четыре" (форум, gutasiho, 10.10.2026). Так -
			// 4 МБ; меню маленькое, разницы в скорости не видно.
			const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(),
			                                                (float)s_dpi, (float)s_dpi);
			s_d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(wnd, D2D1::SizeU(rc.right, rc.bottom)),
			                              s_rt.ReleaseAndGetAddressOf());
		}
		if (s_rt) {
			s_rt->BeginDraw();
			s_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
			s_view.Paint(s_rt.Get(), 96.0f / s_dpi);
			if (s_rt->EndDraw() == D2DERR_RECREATE_TARGET) s_rt.Reset();
		}
		EndPaint(wnd, &ps);
	}

	static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
		switch (msg) {
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
			Paint(wnd);
			return 0;
		case WM_MOUSEMOVE:
			if (!s_tracking) {
				TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, wnd, 0 };
				s_tracking = TrackMouseEvent(&t) != FALSE;
			}
			SetHover(Hit(lp));
			return 0;
		case WM_MOUSELEAVE:
			s_tracking = false;
			if (s_view.pressed < 0) SetHover(-1);
			return 0;
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
			s_view.pressed = Hit(lp);
			SetCapture(wnd);
			InvalidateRect(wnd, nullptr, FALSE);
			return 0;
		case WM_LBUTTONUP:
		case WM_RBUTTONUP: {
			ReleaseCapture();
			const int hit = Hit(lp);
			s_view.pressed = -1;
			if (hit >= 0) Choose(hit);
			else InvalidateRect(wnd, nullptr, FALSE);
			return 0;
		}
		case WM_KEYDOWN:
			switch (wp) {
			case VK_DOWN: case VK_TAB: Move(+1); break;
			case VK_UP: Move(-1); break;
			case VK_HOME: s_view.hover = -1; Move(+1); break;
			case VK_END: s_view.hover = -1; Move(-1); break;
			case VK_RETURN: case VK_SPACE: Choose(s_view.hover); break;
			case VK_ESCAPE: Close(); break;
			}
			return 0;
		case WM_SYSKEYDOWN:
			if (wp == VK_MENU || wp == VK_F10) {
				Close();
				return 0;
			}
			break;
		case WM_CHAR: {
			// Первая буква пункта: один такой - выбрать, несколько - по очереди подсвечивать.
			const wchar_t ch = (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)(wchar_t)wp);
			std::vector<int> found;
			for (int i = 0; i < (int)s_view.items.size(); i++) {
				const auto& t = s_view.items[i].text;
				if (s_view.Selectable(i) && !t.empty() && (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)t[0]) == ch)
					found.push_back(i);
			}
			if (found.size() == 1) Choose(found[0]);
			else if (!found.empty()) {
				auto next = std::upper_bound(found.begin(), found.end(), s_view.hover);
				SetHover(next == found.end() ? found[0] : *next);
			}
			return 0;
		}
		case WM_ACTIVATE:
			if (LOWORD(wp) == WA_INACTIVE) PostMessageW(wnd, WM_CLOSE, 0, 0);
			return 0;
		case WM_TIMER: {
			// Окно впереди сменилось или щёлкнули мимо - закрыть (на случай, когда Windows не дала меню стать активным
			// и WM_ACTIVATE не придёт).
			const HWND fg = GetForegroundWindow(); // NULL - Windows как раз переключает окна
			if (fg == wnd) s_fgAtShow = wnd;
			else if (fg && fg != s_fgAtShow) {
				PostMessageW(wnd, WM_CLOSE, 0, 0);
				return 0;
			}
			POINT p{};
			RECT r{};
			GetCursorPos(&p);
			GetWindowRect(wnd, &r);
			const int buttons[2] = { VK_LBUTTON, VK_RBUTTON };
			for (int i = 0; i < 2; i++) {
				if (!(GetAsyncKeyState(buttons[i]) & 0x8000)) s_buttonUp[i] = true;
				else if (s_buttonUp[i] && !PtInRect(&r, p)) PostMessageW(wnd, WM_CLOSE, 0, 0);
			}
			return 0;
		}
		case WM_SETTINGCHANGE:
		case WM_DPICHANGED:
		case WM_DISPLAYCHANGE:
			PostMessageW(wnd, WM_CLOSE, 0, 0); // тема, масштаб, мониторы - меню уже не то
			return 0;
		case WM_CLOSE:
			DestroyWindow(wnd);
			return 0;
		case WM_DESTROY:
			KillTimer(wnd, 1);
			s_rt.Reset();
			s_tracking = false;
			if (s_wnd == wnd) s_wnd = nullptr;
			return 0;
		}
		return DefWindowProcW(wnd, msg, wp, lp);
	}
};
