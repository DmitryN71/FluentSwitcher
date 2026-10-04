#pragma once

// Флажок раскладки у текстового курсора (каретки).
//
// Без опроса по таймеру: положение каретки проверяется только по событиям - смена окна и фокуса,
// движение каретки в программе на переднем плане (WinEvent), нажатия клавиш и щелчки мыши (хуки движка,
// CaretFlagPoke). Пачка событий сливается в одну проверку через 30-80 мс. В простое процессор не тратится.
//
// Где каретка, узнаём по порядку:
//   1. системная каретка (GetGUIThreadInfo) - почти все обычные программы, мгновенно;
//   2. MSAA OBJID_CARET - Chrome, Electron;
//   3. UI Automation, TextPattern2 / TextPattern - новый Блокнот, Word, WinUI.
// 2 и 3 ходят в чужую программу и могут ждать её ответа, поэтому - в своём потоке (CaretProbe) с таймаутами UIA.
// Каретку не нашли - флажка нет.
//
// Браузеры (Chromium - Chrome, Яндекс, Brave, Edge, программы на Electron; Firefox) - особо: каретка у них бывает и
// вне полей ввода. Firefox держит системную каретку в тексте страницы (флажок ездил по ней при прокрутке), Chromium
// отдаёт через MSAA её последнее место (флажок стоял посреди страницы). Поэтому там флажок - только когда в фокусе
// поле ввода (Editable, по UI Automation). Вся страница браузера - одно окно, так что "в этом окне каретки нет" там не
// запоминается, а не нашли - ещё две попытки: поле могло открываться с анимацией.
//
// Окно флажка - слоистое (UpdateLayeredWindow, плавная прозрачность), не берёт фокус и щелчки, поверх всех.
// В полноэкранных программах, при открытом меню, перетаскивании окна и Alt+Tab флажок прячется.

#include <atlbase.h>
#include <oleacc.h>
#include <UIAutomation.h>
#include <shellapi.h>
#include <ShellScalingApi.h>
#pragma comment(lib, "oleacc.lib")
#pragma comment(lib, "Shcore.lib")

#include "CaretFlagPoke.h"

// Положение каретки из MSAA и UI Automation, в своём потоке. Поток не ждём при выходе: он может висеть
// в вызове к зависшей программе, поэтому живёт отдельно (detach) и делит с окном только State.
class CaretProbe {
public:
	struct Request {
		uint64_t seq = 0;
		HWND focus = nullptr;
		DWORD pid = 0;
		bool uiaFirst = false;
		bool browser = false;  // сначала проверить, что в фокусе поле ввода
		bool system = false;   // у браузера есть системная каретка (Firefox) - вот она:
		RECT systemRc{};
	};
	struct Result {
		uint64_t seq = 0;
		bool ok = false;
		RECT rc{};          // каретка, физические пиксели экрана
		const char* how = "";
		int type = 0;       // браузер: тип элемента в фокусе (UI Automation) и его состояние (MSAA) - для журнала
		DWORD state = 0;
	};

	void Start(HWND notify) {
		m_state->notify = notify;
		std::thread([st = m_state] { Run(st); }).detach();
	}
	void Stop() {
		std::lock_guard lock(m_state->mtx);
		m_state->stop = true;
		m_state->cv.notify_one();
	}
	void Ask(const Request& req) {
		std::lock_guard lock(m_state->mtx);
		m_state->req = req;
		m_state->has = true;
		m_state->cv.notify_one();
	}
	Result Take() {
		std::lock_guard lock(m_state->mtx);
		return m_state->res;
	}

private:
	struct State {
		std::mutex mtx;
		std::condition_variable cv;
		Request req;
		bool has = false;
		bool stop = false;
		Result res;
		HWND notify = nullptr;
	};
	std::shared_ptr<State> m_state = std::make_shared<State>();

	static void Run(std::shared_ptr<State> st) {
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		{
			CComPtr<IUIAutomation> uia;
			if (FAILED(uia.CoCreateInstance(CLSID_CUIAutomation8))) {
				uia.CoCreateInstance(CLSID_CUIAutomation);
			}
			CComPtr<IUIAutomation2> uia2;
			if (uia && SUCCEEDED(uia->QueryInterface(IID_PPV_ARGS(&uia2))) && uia2) {
				// Зависшая программа держит нас не дольше полсекунды.
				uia2->put_ConnectionTimeout(500);
				uia2->put_TransactionTimeout(500);
			}
			while (true) {
				Request req;
				{
					std::unique_lock lock(st->mtx);
					st->cv.wait(lock, [&] { return st->has || st->stop; });
					if (st->stop) break;
					req = st->req;
					st->has = false;
				}
				Result res{ .seq = req.seq };
				RECT rc{};
				if (req.browser) {
					const int editable = uia ? Editable(uia, req.pid, res.type, res.state) : -1;
					if (editable != 1) {
						res.how = editable == 0 ? "not a text field" : "no focus";
					}
					else if (req.system) {
						res = { req.seq, true, req.systemRc, "caret", res.type, res.state };
					}
					else if (Msaa(req.focus, rc)) {
						res = { req.seq, true, rc, "msaa", res.type, res.state };
					}
					else if (Uia(uia, req.pid, rc, true)) {
						res = { req.seq, true, rc, "uia", res.type, res.state };
					}
					else {
						res.how = "text field, no caret";
					}
				}
				else if (!req.uiaFirst && Msaa(req.focus, rc)) {
					res = { req.seq, true, rc, "msaa" };
				}
				else if (uia && Uia(uia, req.pid, rc)) {
					res = { req.seq, true, rc, "uia" };
				}
				else if (req.uiaFirst && Msaa(req.focus, rc)) {
					res = { req.seq, true, rc, "msaa" };
				}
				{
					std::lock_guard lock(st->mtx);
					st->res = res;
				}
				PostMessage(st->notify, CaretFlagDetails::WM_ProbeDone, 0, 0);
			}
		}
		CoUninitialize();
	}

	static bool Msaa(HWND focus, RECT& rc) {
		if (!focus) return false;
		CComPtr<IAccessible> acc;
		if (FAILED(AccessibleObjectFromWindow(focus, (DWORD)OBJID_CARET, IID_IAccessible, (void**)&acc)) || !acc) {
			return false;
		}
		long x = 0, y = 0, w = 0, h = 0;
		VARIANT self;
		self.vt = VT_I4;
		self.lVal = CHILDID_SELF;
		if (FAILED(acc->accLocation(&x, &y, &w, &h, self)) || h <= 1 || (x == 0 && y == 0)) {
			return false;
		}
		rc = { x, y, x + 1, y + h };
		return true;
	}

	// Прямоугольник диапазона текста: первый (или последний - last) из тех, что вернула программа.
	static bool Rects(IUIAutomationTextRange* range, RECT& out, bool last = false) {
		SAFEARRAY* sa = nullptr;
		if (FAILED(range->GetBoundingRectangles(&sa)) || !sa) return false;
		bool ok = false;
		double* d = nullptr;
		LONG lo = 0, hi = -1;
		SafeArrayGetLBound(sa, 1, &lo);
		SafeArrayGetUBound(sa, 1, &hi);
		LONG count = (hi - lo + 1) / 4;
		if (count > 0 && SUCCEEDED(SafeArrayAccessData(sa, (void**)&d))) {
			double* r = d + (last ? (count - 1) * 4 : 0);
			if (r[3] > 1) {
				out = { (LONG)r[0], (LONG)r[1], (LONG)(r[0] + r[2]), (LONG)(r[1] + r[3]) };
				ok = true;
			}
			SafeArrayUnaccessData(sa);
		}
		SafeArrayDestroy(sa);
		return ok;
	}

	static bool Uia(IUIAutomation* uia, DWORD pid, RECT& rc, bool editable = false) {
		CComPtr<IUIAutomationElement> el;
		if (FAILED(uia->GetFocusedElement(&el)) || !el) return false;
		int elPid = 0;
		if (FAILED(el->get_CurrentProcessId(&elPid)) || (DWORD)elPid != pid) return false;
		return UiaCaret(el, rc, editable);
	}

	// Браузер: в фокусе поле ввода? 1 - да, 0 - нет (текст страницы, ссылка, кнопка, список), -1 - фокус не в этой
	// программе или UI Automation не ответил. Текст страницы и поля "только для чтения" - с состоянием MSAA
	// STATE_SYSTEM_READONLY (так их отличают и программы чтения с экрана) или ValuePattern.IsReadOnly.
	static int Editable(IUIAutomation* uia, DWORD pid, int& type, DWORD& state) {
		CComPtr<IUIAutomationElement> el;
		if (FAILED(uia->GetFocusedElement(&el)) || !el) return -1;
		int elPid = 0;
		if (FAILED(el->get_CurrentProcessId(&elPid)) || (DWORD)elPid != pid) return -1;
		CONTROLTYPEID ct = 0;
		el->get_CurrentControlType(&ct);
		type = ct;
		bool readOnly = false, stateKnown = false;
		CComPtr<IUIAutomationLegacyIAccessiblePattern> legacy;
		if (SUCCEEDED(el->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId, IID_PPV_ARGS(&legacy))) && legacy &&
			SUCCEEDED(legacy->get_CurrentState(&state))) {
			stateKnown = true;
			readOnly = (state & STATE_SYSTEM_READONLY) != 0;
		}
		CComPtr<IUIAutomationValuePattern> value;
		BOOL valueReadOnly = TRUE;
		const bool hasValue = SUCCEEDED(el->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value &&
			SUCCEEDED(value->get_CurrentIsReadOnly(&valueReadOnly));
		if (readOnly || (hasValue && valueReadOnly && ct != UIA_DocumentControlTypeId)) return 0;
		if (ct == UIA_EditControlTypeId) return 1;
		if (ct == UIA_DocumentControlTypeId) return stateKnown ? 1 : 0; // страница, которую можно править (редактор)
		// Остальное (поле с подсказками - ComboBox, contenteditable - группа): только с изменяемым значением.
		// Выпадающий список (select) - ComboBox со значением "только для чтения".
		return hasValue && !valueReadOnly ? 1 : 0;
	}

	// Каретка в текстовом элементе UI Automation. editable - элемент уже проверен как поле ввода (браузер): без
	// символов у каретки - у края поля, какого бы типа он ни был.
	static bool UiaCaret(IUIAutomationElement* el, RECT& rc, bool editable = false) {
		CComPtr<IUIAutomationTextRange> range;
		CComPtr<IUIAutomationTextPattern2> tp2;
		CComPtr<IUIAutomationTextPattern> tp;
		if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&tp2))) && tp2) {
			BOOL active = FALSE;
			tp2->GetCaretRange(&active, &range);
			tp = tp2;
		}
		if (!tp) {
			el->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&tp));
		}
		if (!range && tp) {
			// Без TextPattern2: конец выделения (пустое выделение - и есть каретка).
			CComPtr<IUIAutomationTextRangeArray> sel;
			int n = 0;
			if (SUCCEEDED(tp->GetSelection(&sel)) && sel && SUCCEEDED(sel->get_Length(&n)) && n > 0 &&
				SUCCEEDED(sel->GetElement(0, &range)) && range) {
				range->MoveEndpointByRange(TextPatternRangeEndpoint_Start, range, TextPatternRangeEndpoint_End);
			}
		}

		RECT r{};
		if (range) {
			if (Rects(range, r)) {
				rc = { r.left, r.top, r.left + 1, r.bottom };
				return true;
			}
			// Пустой диапазон часто без прямоугольника: символ после каретки - она у его левого края...
			CComPtr<IUIAutomationTextRange> ch;
			if (SUCCEEDED(range->Clone(&ch)) && ch && SUCCEEDED(ch->ExpandToEnclosingUnit(TextUnit_Character)) && Rects(ch, r)) {
				rc = { r.left, r.top, r.left + 1, r.bottom };
				return true;
			}
			// ...а в конце текста - символ перед ней, каретка у его правого края.
			ch.Release();
			int moved = 0;
			if (SUCCEEDED(range->Clone(&ch)) && ch &&
				SUCCEEDED(ch->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -1, &moved)) &&
				moved == -1 && Rects(ch, r)) {
				rc = { r.right, r.top, r.right + 1, r.bottom };
				return true;
			}
		}

		// Символов у каретки нет: поле пустое или программа не говорит, где они (поля адреса и темы в eM Client).
		// Только для полей ввода: текст есть - каретка в его конце (при наборе она там); нет - у левого края.
		CONTROLTYPEID type = 0;
		if (!editable &&
			(FAILED(el->get_CurrentControlType(&type)) || (type != UIA_EditControlTypeId && type != UIA_DocumentControlTypeId))) {
			return false;
		}
		CComPtr<IUIAutomationTextRange> doc;
		if (tp && SUCCEEDED(tp->get_DocumentRange(&doc)) && doc && Rects(doc, r, true)) {
			rc = { r.right, r.top, r.right + 1, r.bottom };
			return true;
		}
		RECT box{};
		if (FAILED(el->get_CurrentBoundingRectangle(&box)) || box.bottom - box.top < 8 || box.right <= box.left) return false;
		LONG pad = std::max<LONG>(2, (box.bottom - box.top) / 6);
		rc = { box.left + pad, box.top + pad, box.left + pad + 1, box.bottom - pad };
		return true;
	}
};

class CaretFlag {
public:
	CaretFlag() {
		s_inst = this;
		WNDCLASSEXW wc = { sizeof(wc) };
		wc.lpfnWndProc = WndProc;
		wc.hInstance = GetModuleHandle(nullptr);
		wc.lpszClassName = L"FluentSwitcher_CaretFlag";
		RegisterClassExW(&wc);
		m_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
			wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
		IFW_LOG(m_wnd != nullptr);
		m_probe.Start(m_wnd);
		CaretFlagDetails::g_wnd = m_wnd;
		Refresh();
	}
	~CaretFlag() {
		CaretFlagDetails::g_wnd = nullptr;
		Unhook();
		m_probe.Stop();
		if (m_wnd) DestroyWindow(m_wnd);
		s_inst = nullptr;
	}

	// Сообщение о раскладке (WM_LayNotif): lay - новая; 0 - перечитать настройки (окно настроек, вкл./выкл.).
	void OnLayout(HKL lay) {
		if (lay == 0) {
			Refresh();
			return;
		}
		Poke(0);
	}

	// Настройки могли смениться.
	void Refresh() {
		m_imgKey.clear();
		int mode = conf_get_unsafe()->caret_flag;
		if (mode == 0) {
			Unhook();
			Hide();
			return;
		}
		if (!m_hookSys) {
			auto flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
			m_hookSys = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_MINIMIZEEND, nullptr, OnEvent, 0, 0, flags);
			m_hookFocus = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr, OnEvent, 0, 0, flags);
			IFW_LOG(m_hookSys && m_hookFocus);
		}
		Brief();
		EventPoke(0);
	}

private:
	static inline CaretFlag* s_inst = nullptr;
	HWND m_wnd = nullptr;
	HWINEVENTHOOK m_hookSys = nullptr;
	HWINEVENTHOOK m_hookFocus = nullptr;
	std::vector<HWINEVENTHOOK> m_procHooks;
	DWORD m_hookedPid = 0;
	CaretProbe m_probe;
	uint64_t m_seq = 0;
	HWND m_askedFg = nullptr;
	HWND m_askedFocus = nullptr;
	// Поток (MSAA, UIA) не нашёл каретку в этом окне: нажатия и щелчки его больше не спрашивают - только
	// события Windows (фокус, окно, каретка). Иначе в окне без текста (почта, проводник) каждый щелчок
	// и прокрутка ходили бы в чужую программу.
	HWND m_noCaretFocus = nullptr;
	bool m_eventPoked = false;
	bool m_askedBrowser = false;
	int m_retry = 0;          // браузер: сколько раз уже переспросили после неудачи
	bool m_retrying = false;  // эта проверка - повторная
	int m_lastType = -1;      // браузер: что было в фокусе в прошлый раз (для журнала)
	DWORD m_lastState = 0;

	bool m_visible = false;
	HWND m_shownFg = nullptr;
	ULONGLONG m_showUntil = 0;   // режим "ненадолго": до этого времени
	HKL m_lay = 0;
	std::wstring m_imgKey;       // какая картинка в окне
	RECT m_bbox{};               // видимая (непрозрачная) часть картинки
	const char* m_lastHow = "";

	enum : UINT_PTR { TimerUpdate = 1, TimerBrief = 2 };

	void Poke(UINT delay) {
		SetTimer(m_wnd, TimerUpdate, delay, nullptr);
	}
	// Толчок от события Windows: может спрашивать и окно, где каретки не нашлось.
	void EventPoke(UINT delay) {
		m_eventPoked = true;
		Poke(delay);
	}

	// Режим "ненадолго": показать на caret_flag_brief_ms.
	void Brief() {
		auto cfg = conf_get_unsafe();
		if (cfg->caret_flag != 2) return;
		UINT ms = (UINT)std::clamp(cfg->caret_flag_brief_ms, 300, 60000);
		m_showUntil = GetTickCount64() + ms;
		SetTimer(m_wnd, TimerBrief, ms + 100, nullptr);
	}

	void Hide() {
		if (m_visible) {
			ShowWindow(m_wnd, SW_HIDE);
			m_visible = false;
		}
		m_shownFg = nullptr;
	}

	void Unhook() {
		for (auto h : { m_hookSys, m_hookFocus }) if (h) UnhookWinEvent(h);
		m_hookSys = m_hookFocus = nullptr;
		HookProcess(0);
	}

	// События движения каретки - только от программы на переднем плане: события всех программ сразу -
	// лишняя работа (браузеры шлют их сотнями).
	void HookProcess(DWORD pid) {
		for (auto h : m_procHooks) UnhookWinEvent(h);
		m_procHooks.clear();
		m_hookedPid = pid;
		if (!pid) return;
		auto flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
		for (auto [from, to] : { std::pair{ EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE },
								 std::pair{ EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE },
								 std::pair{ EVENT_OBJECT_TEXTSELECTIONCHANGED, EVENT_OBJECT_TEXTSELECTIONCHANGED } }) {
			if (auto h = SetWinEventHook(from, to, nullptr, OnEvent, pid, 0, flags)) m_procHooks.push_back(h);
		}
	}

	static void CALLBACK OnEvent(HWINEVENTHOOK, DWORD event, HWND, LONG idObject, LONG, DWORD, DWORD) {
		auto* self = s_inst;
		if (!self) return;
		switch (event) {
		case EVENT_SYSTEM_FOREGROUND:
		case EVENT_OBJECT_FOCUS:
			self->Brief();
			self->EventPoke(event == EVENT_SYSTEM_FOREGROUND ? 80 : 40);
			return;
		case EVENT_SYSTEM_MOVESIZESTART:
		case EVENT_SYSTEM_MINIMIZESTART:
		case EVENT_SYSTEM_MENUSTART:
		case EVENT_SYSTEM_MENUPOPUPSTART:
		case EVENT_SYSTEM_SWITCHSTART:
			self->Hide();
			return;
		case EVENT_SYSTEM_MOVESIZEEND:
		case EVENT_SYSTEM_MINIMIZEEND:
		case EVENT_SYSTEM_MENUEND:
		case EVENT_SYSTEM_MENUPOPUPEND:
		case EVENT_SYSTEM_SWITCHEND:
		case EVENT_SYSTEM_SCROLLINGEND:
			self->EventPoke(80);
			return;
		case EVENT_OBJECT_SHOW:
		case EVENT_OBJECT_HIDE:
		case EVENT_OBJECT_LOCATIONCHANGE:
			if (idObject == OBJID_CARET) self->EventPoke(30);
			return;
		case EVENT_OBJECT_TEXTSELECTIONCHANGED:
			self->EventPoke(30);
			return;
		}
	}

	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		auto* self = s_inst;
		if (self && hwnd == self->m_wnd) {
			switch (msg) {
			case CaretFlagDetails::WM_Poke:
				self->Poke((UINT)wParam);
				return 0;
			case WM_TIMER:
				KillTimer(hwnd, wParam);
				if (wParam == TimerUpdate) self->Update();
				if (wParam == TimerBrief) self->Update();
				return 0;
			case CaretFlagDetails::WM_ProbeDone:
				self->OnProbe();
				return 0;
			case WM_DISPLAYCHANGE:
				self->EventPoke(200);
				return 0;
			case WM_DPICHANGED:
				return 0; // размер окна ставим сами
			case WM_MOUSEACTIVATE:
				return MA_NOACTIVATE;
			}
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	static bool IsClass(HWND hwnd, std::initializer_list<const wchar_t*> names) {
		wchar_t cls[128]{};
		if (!hwnd || !GetClassNameW(hwnd, cls, (int)std::size(cls))) return false;
		for (auto n : names) if (wcscmp(cls, n) == 0) return true;
		return false;
	}

	// Браузер: страница Chromium (Chrome, Яндекс, Brave, Edge, Electron) или Firefox. Адресная строка Chromium - не
	// страница (Chrome_WidgetWin_1), с ней - как с обычной программой.
	static bool Browser(HWND fg, HWND focus) {
		return IsClass(focus, { L"Chrome_RenderWidgetHostHWND", L"MozillaWindowClass" }) ||
			(!focus && IsClass(fg, { L"MozillaWindowClass" }));
	}

	// Программы, где системная каретка есть, но не там (или её нет вовсе): сразу UI Automation.
	static bool UiaFirst(HWND fg, HWND focus) {
		return IsClass(fg, { L"ApplicationFrameWindow" }) ||
			IsClass(focus, { L"RichEditD2DPT", L"Windows.UI.Core.CoreWindow", L"Microsoft.UI.Content.DesktopChildSiteBridge",
				L"Windows.UI.Input.InputSite.WindowClass" });
	}

	static bool IsFullscreen(HWND fg) {
		QUERY_USER_NOTIFICATION_STATE st{};
		if (SUCCEEDED(SHQueryUserNotificationState(&st)) &&
			(st == QUNS_RUNNING_D3D_FULL_SCREEN || st == QUNS_PRESENTATION_MODE || st == QUNS_BUSY)) {
			return true;
		}
		if (IsClass(fg, { L"Progman", L"WorkerW" })) return false;
		RECT wr{};
		MONITORINFO mi = { sizeof(mi) };
		if (!GetWindowRect(fg, &wr) || !GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi)) return false;
		return wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top && wr.right >= mi.rcMonitor.right &&
			wr.bottom >= mi.rcMonitor.bottom && !(GetWindowLongW(fg, GWL_STYLE) & WS_CAPTION);
	}

	// Системная каретка: клиентские координаты её окна -> экран, как его видит это окно -> физические пиксели.
	// Окно, которое Windows масштабирует само (не знает о DPI), видит экран в своих "логических" точках:
	// переводим по его же прямоугольнику, логическому и физическому (LogicalToPhysicalPointForPerMonitorDPI
	// для такого окна в tools\test_caret.cpp не пересчитал ничего).
	static bool SystemCaret(const GUITHREADINFO& gti, RECT& out) {
		HWND w = gti.hwndCaret;
		if (!w || gti.rcCaret.bottom - gti.rcCaret.top < 2) return false;
		POINT a{ gti.rcCaret.left, gti.rcCaret.top };
		POINT b{ gti.rcCaret.left, gti.rcCaret.bottom };
		RECT logical{}, physical{};
		auto old = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(w));
		bool ok = ClientToScreen(w, &a) && ClientToScreen(w, &b) && GetWindowRect(w, &logical);
		SetThreadDpiAwarenessContext(old);
		auto ctx = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		ok = ok && GetWindowRect(w, &physical);
		SetThreadDpiAwarenessContext(ctx);
		if (!ok) return false;
		auto map = [](LONG v, LONG logFrom, LONG logTo, LONG physFrom, LONG physTo) {
			LONG len = logTo - logFrom;
			return len > 0 ? physFrom + (LONG)std::lround(double(v - logFrom) * (physTo - physFrom) / len) : v;
		};
		LONG x = map(a.x, logical.left, logical.right, physical.left, physical.right);
		LONG top = map(a.y, logical.top, logical.bottom, physical.top, physical.bottom);
		LONG bottom = map(b.y, logical.top, logical.bottom, physical.top, physical.bottom);
		out = { x, top, x + 1, bottom };
		return out.bottom > out.top;
	}

	void Update() {
		const bool eventDriven = std::exchange(m_eventPoked, false);
		auto cfg = conf_get_unsafe();
		int mode = cfg->caret_flag;
		if (mode == 0 || cfg->flagsSet == ProgramConfig::showFlags_Nothing ||
			cfg->flagsSet == ProgramConfig::showFlags_AppIcon) {
			return Hide();
		}
		HWND fg = GetForegroundWindow();
		if (!fg || IsFullscreen(fg)) return Hide();

		DWORD pid = 0;
		DWORD tid = GetWindowThreadProcessId(fg, &pid);
		if (pid != m_hookedPid) HookProcess(pid);

		// Раскладка сменилась - в режиме "ненадолго" это повод показаться.
		HKL lay = Utils::GetFocusedWndInfo().lay;
		if (lay && lay != m_lay) {
			if (m_lay) Brief();
			m_lay = lay;
		}
		if (mode == 2 && GetTickCount64() > m_showUntil) return Hide();

		GUITHREADINFO gti = { sizeof(gti) };
		if (!GetGUIThreadInfo(tid, &gti)) return Hide();
		if (gti.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE | GUI_INMOVESIZE)) return Hide();

		if (!std::exchange(m_retrying, false)) m_retry = 0;
		RECT caret{};
		const bool uiaFirst = UiaFirst(fg, gti.hwndFocus);
		const bool browser = Browser(fg, gti.hwndFocus);
		const bool system = !uiaFirst && SystemCaret(gti, caret);
		if (system && !browser) {
			++m_seq; // ответ потока, если он ещё идёт, уже не нужен
			return Place(fg, caret, "caret");
		}
		// Остальное - в потоке CaretProbe; пока ждём, флажок остаётся, только если окно то же.
		HWND focus = gti.hwndFocus ? gti.hwndFocus : fg;
		if (!eventDriven && !browser && focus == m_noCaretFocus) return Hide();
		if (fg != m_shownFg) Hide();
		m_askedFg = fg;
		m_askedFocus = focus;
		m_askedBrowser = browser;
		m_probe.Ask({ ++m_seq, focus, pid, uiaFirst, browser, system, caret });
	}

	void OnProbe() {
		auto res = m_probe.Take();
		if (res.seq != m_seq || GetForegroundWindow() != m_askedFg) return; // устарел
		m_noCaretFocus = res.ok || m_askedBrowser ? nullptr : m_askedFocus;
		if (m_askedBrowser && (res.type != m_lastType || res.state != m_lastState)) {
			LOG_ANY("caret flag: browser focus type {} state 0x{:x}", res.type, res.state);
			m_lastType = res.type;
			m_lastState = res.state;
		}
		if (!res.ok) {
			const char* why = *res.how ? res.how : "no caret";
			if (why != m_lastHow) LOG_ANY("caret flag: {}", why);
			m_lastHow = why;
			// Браузер: поле могло ещё ехать на место (открывается с анимацией), а Chromium - только включать
			// специальные возможности. Ещё две попытки.
			if (m_askedBrowser && m_retry < 2) {
				m_retry++;
				m_retrying = true;
				EventPoke(m_retry == 1 ? 200 : 600);
			}
			return Hide();
		}
		Place(m_askedFg, res.rc, res.how);
	}

	// Картинка флага текущей раскладки нужного размера - в окно (если она другая).
	bool PrepareImage(int px) {
		if (!m_lay) return false;
		bool gray = !g_enabled.IsEnabled();
		auto id = Utils::GetNameForHKL_simple(m_lay);
		auto cfg = conf_get_unsafe();
		const int opacity = std::clamp(cfg->caret_flag_opacity, 10, 100);
		auto key = std::format(L"{}|{}|{}|{}|{}|{}", id, px, gray, StrUtils::Convert(cfg->flagsSet), cfg->useBritishFlag, opacity);
		if (key == m_imgKey) return true;
		auto img = IconMgr::Inst().GetImage(id.c_str(), px, gray);
		if (!img || !img->IsOk()) return false;

		int w = img->width, h = img->height;
		BITMAPINFO bi{};
		bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
		void* bits = nullptr;
		HDC screen = GetDC(nullptr);
		HDC mem = CreateCompatibleDC(screen);
		HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (!bmp) {
			DeleteDC(mem);
			ReleaseDC(nullptr, screen);
			return false;
		}
		// RGBA -> BGRA с умноженной на альфу яркостью (так хочет UpdateLayeredWindow); заодно видимая часть.
		RECT bbox{ w, h, 0, 0 };
		auto* src = img->data;
		auto* dst = (unsigned char*)bits;
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++, src += 4, dst += 4) {
				unsigned a = src[3];
				dst[0] = (unsigned char)(src[2] * a / 255);
				dst[1] = (unsigned char)(src[1] * a / 255);
				dst[2] = (unsigned char)(src[0] * a / 255);
				dst[3] = (unsigned char)a;
				if (a > 40) {
					bbox.left = std::min<LONG>(bbox.left, x);
					bbox.top = std::min<LONG>(bbox.top, y);
					bbox.right = std::max<LONG>(bbox.right, x + 1);
					bbox.bottom = std::max<LONG>(bbox.bottom, y + 1);
				}
			}
		}
		if (bbox.right <= bbox.left) bbox = { 0, 0, w, h };
		auto old = SelectObject(mem, bmp);
		SIZE size{ w, h };
		POINT zero{};
		BLENDFUNCTION bf{ AC_SRC_OVER, 0, (BYTE)(opacity * 255 / 100), AC_SRC_ALPHA };
		bool ok = UpdateLayeredWindow(m_wnd, screen, nullptr, &size, mem, &zero, 0, &bf, ULW_ALPHA);
		IFW_LOG(ok);
		SelectObject(mem, old);
		DeleteObject(bmp);
		DeleteDC(mem);
		ReleaseDC(nullptr, screen);
		if (!ok) return false;
		m_imgKey = key;
		m_bbox = bbox;
		return true;
	}

	void Place(HWND fg, const RECT& caret, const char* how) {
		if (how != m_lastHow) {
			LOG_ANY("caret flag: {}", how);
			m_lastHow = how;
		}
		// Каретка за пределами окна (прокрутили, устарела) - флажка нет.
		RECT wr{};
		if (!GetWindowRect(fg, &wr) || caret.left < wr.left - 4 || caret.left > wr.right + 4 || caret.top < wr.top - 4 ||
			caret.bottom > wr.bottom + 4) {
			return Hide();
		}
		HMONITOR mon = MonitorFromPoint({ caret.left, caret.top }, MONITOR_DEFAULTTONEAREST);
		UINT dpiX = 96, dpiY = 96;
		GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
		int px = MulDiv(std::clamp(conf_get_unsafe()->caret_flag_size, 12, 64), dpiX, 96);
		if (!PrepareImage(px)) return Hide();

		// Под кареткой (или над ней - caret_flag_place), левым краем у неё. Не помещается у края рабочей
		// области - с другой стороны строки.
		int s = std::max(1, MulDiv(1, dpiX, 96));
		int x = caret.left - m_bbox.left;
		int below = caret.bottom + 2 * s - m_bbox.top;
		int above = caret.top - 2 * s - m_bbox.bottom;
		bool wantAbove = conf_get_unsafe()->caret_flag_place == 1;
		int y = wantAbove ? above : below;
		MONITORINFO mi = { sizeof(mi) };
		if (MonitorFromPoint({ caret.left, caret.top }, MONITOR_DEFAULTTONULL) && GetMonitorInfoW(mon, &mi)) {
			if (wantAbove && above + m_bbox.top < mi.rcWork.top) y = below;
			if (!wantAbove && below + m_bbox.bottom > mi.rcWork.bottom) y = above;
		}
		SetWindowPos(m_wnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
		m_visible = true;
		m_shownFg = fg;
		if (conf_get_unsafe()->caret_flag == 2) {
			auto left = m_showUntil > GetTickCount64() ? m_showUntil - GetTickCount64() : 0;
			SetTimer(m_wnd, TimerBrief, (UINT)left + 20, nullptr);
		}
	}
};
