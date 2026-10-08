#pragma once

// Флажок раскладки у текстового курсора (каретки).
//
// Без опроса по таймеру: положение каретки проверяется только по событиям - смена окна и фокуса,
// движение каретки в программе на переднем плане (WinEvent), нажатия клавиш и щелчки мыши (хуки движка,
// CaretFlagPoke). Пачка событий сливается в одну проверку через 30-80 мс. В простое процессор не тратится.
//
// Где каретка, узнаём по порядку:
//   1. системная каретка (GetGUIThreadInfo) - почти все обычные программы и новый Блокнот, мгновенно; ниже 4 точек
//      (Telegram) - не каретка;
//   2. MSAA OBJID_CARET - Chrome, Electron;
//   3. UI Automation, TextPattern2 / TextPattern - Word, WinUI, приложения Магазина (у них - сразу, UiaFirst:
//      системная каретка там не на месте).
// 2 и 3 ходят в чужую программу и могут ждать её ответа, поэтому - в своём потоке (CaretProbe) с таймаутами UIA.
// Каретку не нашли - флажка нет.
//
// Браузеры (Chromium - Chrome, Яндекс, Brave, Edge, программы на Electron; Firefox) - особо: каретка у них бывает и
// вне полей ввода. Firefox держит системную каретку в тексте страницы (флажок ездил по ней при прокрутке), Chromium
// отдаёт через MSAA её последнее место (флажок стоял посреди страницы). Поэтому там флажок - только когда в фокусе
// поле ввода (Editable, по UI Automation), а каретка - внутри этого поля и внутри видимой части страницы (Inside):
// иначе она старая (после щелчка из адресной строки Chromium ещё отдаёт её место там) или поле уехало при прокрутке
// (у Firefox каретка такого поля сначала попадает на панели браузера, ещё внутри окна). Так же - окна-рамки Chromium
// (адресная строка, программы на Electron). Вся страница браузера - одно окно, так что "в этом окне каретки нет" там
// не запоминается, а не нашли - ещё две попытки: поле могло открываться с анимацией. После щелчка - ещё одна проверка
// через 350 мс: фокус внутри Chromium переезжает не сразу.
// Редактор на странице (contenteditable; тело письма в eM Client) браузеры описывают группой без значения: в Chromium
// это поле (всё, где печатать нельзя, у него "только для чтения"), в Firefox - если у него системная каретка. Firefox
// со специальными возможностями, запрещёнными в его настройках, не говорит ничего - тогда его системная каретка без
// проверок, как до 1.4.2 (BrowserCaret).
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
		int type = 0;       // браузер: тип элемента в фокусе (UI Automation) и его состояние (MSAA)
		DWORD state = 0;
		std::string detail; // браузер: что нашли - для журнала
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
					res = BrowserCaret(uia, req);
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
			s_focusCache = {};
		}
		CoUninitialize();
	}

	// Браузер: каретка, если в фокусе поле ввода, и она в нём и в видимой части страницы; detail - для журнала.
	static Result BrowserCaret(IUIAutomation* uia, const Request& req) {
		Result res{ .seq = req.seq };
		RECT rc{};
		CComPtr<IUIAutomationElement> el;
		int editable = uia ? Editable(uia, req, res.type, res.state, el) : -1;
		// Редактор на странице в Firefox (contenteditable - группа без значения): поле, если у него системная каретка -
		// Firefox заводит её только там, где можно печатать (и в тексте страницы при F7, но страница - Document "только
		// для чтения").
		// Chromium (браузеры, eM Client, Electron) редактор (contenteditable) описывает так же, а всё, где печатать
		// нельзя, - "только для чтения" (страница, текст): группа без этого - поле; каретка внутри неё проверяется ниже.
		if (editable == 2) editable = req.system || Chromium(el) ? 1 : 0;
		// Firefox без специальных возможностей ("Запретить службам специальных возможностей доступ к браузеру",
		// accessibility.force_disabled - бывает в "усиленных" настройках): про страницу он ничего не говорит, в фокусе
		// для UI Automation - само окно. Тогда, как до 1.4.2, - его системная каретка, без проверки поля.
		const bool blind = editable != 1 && req.system && !Speaks(el);
		const char* how = nullptr;
		if (blind) {
			rc = req.systemRc;
			res = { req.seq, true, rc, "caret, no accessibility", res.type, res.state };
		}
		else if (editable != 1) {
			res.how = editable == 0 ? "not a text field" : "no focus";
		}
		else if (req.system) {
			rc = req.systemRc;
			how = "caret";
		}
		else if (Msaa(req.focus, rc)) {
			how = "msaa";
		}
		else if (UiaCaret(el, rc, true)) {
			how = "uia";
		}
		else {
			res.how = "text field, no caret";
		}
		if (how) {
			if (Inside(uia, el, rc)) res = { req.seq, true, rc, how, res.type, res.state };
			// Поле поиска на домашней странице Firefox - кнопка с нарисованной кареткой: набор Firefox отдаёт адресной
			// строке, а системную каретку ставит в то поле на странице. Фокус в адресной строке, своя каретка Firefox - не
			// в ней: это оно (в адресной строке, куда щёлкнули, каретка всегда внутри).
			else if (req.system && Handoff(el)) res = { req.seq, true, rc, "caret (search on the page)", res.type, res.state };
			else res.how = "caret outside the field or the page";
		}
		// Что браузер считает фокусом и где каретка - в журнал (по нему видно, почему флажок есть или нет).
		wchar_t cls[64] = {};
		GetClassNameW(req.focus, cls, 64);
		RECT box{};
		BOOL kb = FALSE;
		CComBSTR fw;
		if (el) {
			el->get_CurrentBoundingRectangle(&box);
			el->get_CurrentHasKeyboardFocus(&kb);
			el->get_CurrentFrameworkId(&fw);
		}
		res.detail = std::format("browser {}: {} type {} state 0x{:x} keyboard {} field ({},{})-({},{}) caret ({},{})-({},{}) -> {}",
			StrUtils::Convert(std::wstring(cls)), StrUtils::Convert(std::wstring(fw ? (const wchar_t*)fw : L"")), res.type,
			res.state, kb != FALSE, box.left, box.top, box.right, box.bottom, rc.left, rc.top, rc.right, rc.bottom,
			res.ok ? res.how : (*res.how ? res.how : "no caret"));
		return res;
	}

	// Элемент в фокусе - от самого браузера (его специальные возможности работают), а не окно, которое описывает
	// за него Windows.
	static bool Speaks(IUIAutomationElement* el) {
		CComBSTR fw;
		if (!el || FAILED(el->get_CurrentFrameworkId(&fw)) || !fw) return false;
		return wcscmp(fw, L"Gecko") == 0 || wcscmp(fw, L"Chrome") == 0;
	}

	// Адресная строка Firefox.
	static bool Handoff(IUIAutomationElement* el) {
		CComBSTR fw, cls;
		return el && SUCCEEDED(el->get_CurrentFrameworkId(&fw)) && fw && wcscmp(fw, L"Gecko") == 0 &&
			SUCCEEDED(el->get_CurrentClassName(&cls)) && cls && wcsstr(cls, L"urlbar") != nullptr;
	}

	static bool Chromium(IUIAutomationElement* el) {
		CComBSTR fw;
		return el && SUCCEEDED(el->get_CurrentFrameworkId(&fw)) && fw && wcscmp(fw, L"Chrome") == 0;
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
		// Каретка в 2-3 точки - не каретка текста: Telegram (Qt) ставит такую над строкой для окна ввода иероглифов, а его
		// каретку говорит UI Automation (Дмитрий 08.10.2026: флажок прыгал из-под каретки на строку).
		if (FAILED(acc->accLocation(&x, &y, &w, &h, self)) || h < 4 || (x == 0 && y == 0)) {
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
	// программе или UI Automation не ответил, 2 - может быть: группа с текстом без значения, не "только для чтения"
	// (редактор на странице - contenteditable, тело письма в eM Client; в Firefox решает его системная каретка, в
	// Chromium - да: BrowserCaret). Текст страницы и поля "только для чтения" - с состоянием MSAA
	// STATE_SYSTEM_READONLY (так их отличают и программы чтения с экрана) или ValuePattern.IsReadOnly.
	static int Editable(IUIAutomation* uia, const Request& req, int& type, DWORD& state, CComPtr<IUIAutomationElement>& el) {
		if (FAILED(uia->GetFocusedElement(&el)) || !el) return -1;
		int elPid = 0;
		if (FAILED(el->get_CurrentProcessId(&elPid))) return -1;
		// WhatsApp (WinUI 3 со страницей Chromium внутри): Windows называет фокусом рамку - мост WinUI или окно
		// Chrome_WidgetWin_0, не в фокусе и без размеров, - а поле ввода на странице есть в дереве самого окна.
		if (CComPtr<IUIAutomationElement> inner = FocusInWindow(uia, req.focus, el)) {
			el = inner;
		}
		else if ((DWORD)elPid != req.pid)
			return -1;
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
		// Скрыт (поле закрыли) или не в фокусе на самом деле: браузер иногда ещё отдаёт прежний элемент.
		BOOL keyboard = FALSE;
		el->get_CurrentHasKeyboardFocus(&keyboard);
		if (stateKnown && (state & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN))) return 0;
		if (stateKnown && !keyboard && !(state & STATE_SYSTEM_FOCUSED)) return 0;
		if (ct == UIA_EditControlTypeId) return 1;
		if (ct == UIA_DocumentControlTypeId) return stateKnown ? 1 : 0; // страница, которую можно править (редактор)
		// Остальное (поле с подсказками - ComboBox, contenteditable - группа): только с изменяемым значением.
		// Выпадающий список (select) - ComboBox со значением "только для чтения".
		if (hasValue) return valueReadOnly ? 0 : 1;
		CComPtr<IUIAutomationTextPattern> text;
		return ct == UIA_GroupControlTypeId && stateKnown &&
			SUCCEEDED(el->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text))) && text ? 2 : 0;
	}

	// Найденный FocusInWindow элемент - только в потоке CaretProbe; отпускается до CoUninitialize (Run): после него
	// Release мог бы пойти в уже выгруженную UI Automation.
	struct FocusCache {
		HWND root = nullptr;
		CComPtr<IUIAutomationElement> el;
		ULONGLONG missedAt = 0; // поиск ничего не нашёл (когда кончился): снова - не раньше чем через полсекунды
	};
	static inline thread_local FocusCache s_focusCache;

	// Фокус по Windows - рамка, а не поле (focused: тип Pane, Win32, не в фокусе клавиатуры или без размеров): элемент с
	// фокусом клавиатуры в дереве окна focus (поиск по всему дереву страницы - долгий: найденный помнится, пока у него
	// фокус - ушёл с него, искать сразу; не нашли - снова не раньше чем через полсекунды). Иначе - nullptr.
	static CComPtr<IUIAutomationElement> FocusInWindow(IUIAutomation* uia, HWND focus, IUIAutomationElement* focused) {
		CONTROLTYPEID ct = 0;
		CComBSTR fw;
		BOOL kb = FALSE;
		RECT box{};
		if (!focused || FAILED(focused->get_CurrentControlType(&ct)) || ct != UIA_PaneControlTypeId ||
			FAILED(focused->get_CurrentFrameworkId(&fw)) || !fw || wcscmp(fw, L"Win32") != 0)
			return nullptr;
		focused->get_CurrentHasKeyboardFocus(&kb);
		focused->get_CurrentBoundingRectangle(&box);
		if (kb && box.right > box.left) return nullptr;
		const HWND root = focus ? GetAncestor(focus, GA_ROOT) : nullptr;
		if (!root) return nullptr;
		auto& cache = s_focusCache;
		if (cache.root == root && cache.el) {
			BOOL still = FALSE;
			if (SUCCEEDED(cache.el->get_CurrentHasKeyboardFocus(&still)) && still) return cache.el;
			cache.el = nullptr; // фокус перешёл (поиск чатов - поле сообщения): искать сразу
			cache.missedAt = 0;
		}
		if (cache.root == root && cache.missedAt && GetTickCount64() - cache.missedAt < 500) return nullptr;
		cache.root = root;
		cache.el = nullptr;
		CComPtr<IUIAutomationElement> top;
		CComPtr<IUIAutomationCondition> cond;
		VARIANT yes;
		yes.vt = VT_BOOL;
		yes.boolVal = VARIANT_TRUE;
		CComPtr<IUIAutomationElement> found;
		if (FAILED(uia->ElementFromHandle(root, &top)) || !top ||
			FAILED(uia->CreatePropertyCondition(UIA_HasKeyboardFocusPropertyId, yes, &cond)) || !cond ||
			FAILED(top->FindFirst(TreeScope_Descendants, cond, &found)) || !found) {
			cache.missedAt = GetTickCount64(); // после поиска: он сам бывает дольше полсекунды
			return nullptr;
		}
		cache.el = found;
		cache.missedAt = 0;
		return found;
	}

	// Браузер: каретка внутри поля в фокусе и внутри видимой части страницы (ближайший документ над полем - страница
	// или её фрейм; его прямоугольник - то, что видно). Адресная строка в документе не лежит - только поле. Каретка
	// чуть за краем поля - в нём: пустое поле сообщения WhatsApp называет её на 6 точек левее поля и выше и ниже него
	// (Дмитрий 08.10.2026: флажка не было); запас - полвысоты каретки, и она переносится внутрь.
	static bool Inside(IUIAutomation* uia, IUIAutomationElement* el, RECT& caret) {
		const LONG slack = std::max<LONG>(4, (caret.bottom - caret.top) / 2), x = caret.left,
		           cy = (caret.top + caret.bottom) / 2;
		auto in = [&](const RECT& r) {
			return r.right > r.left && x >= r.left - slack && x <= r.right + slack && cy >= r.top - slack && cy <= r.bottom + slack;
		};
		RECT box{};
		if (FAILED(el->get_CurrentBoundingRectangle(&box)) || !in(box)) return false;
		const RECT was = caret;
		caret.left = std::clamp(caret.left, box.left, box.right - 1);
		caret.right = caret.left + 1;
		caret.top = std::max(caret.top, box.top);
		caret.bottom = std::min(caret.bottom, box.bottom);
		if (caret.bottom - caret.top < 4) caret = was; // поле ниже строки: как было
		CComPtr<IUIAutomationTreeWalker> walker;
		if (FAILED(uia->get_ControlViewWalker(&walker)) || !walker) return true;
		CComPtr<IUIAutomationElement> cur = el;
		for (int depth = 0; depth < 40 && cur; depth++) {
			CONTROLTYPEID type = 0;
			if (depth > 0 && SUCCEEDED(cur->get_CurrentControlType(&type)) && type == UIA_DocumentControlTypeId) {
				RECT page{};
				return FAILED(cur->get_CurrentBoundingRectangle(&page)) || in(page);
			}
			CComPtr<IUIAutomationElement> parent;
			if (FAILED(walker->GetParentElement(cur, &parent))) break;
			cur = parent;
		}
		return true;
	}

	// PowerShell ISE (его редактор - из Visual Studio 2010, WpfTextView): UI Automation отдаёт прямоугольники текста в
	// координатах самого редактора - точки (1/96 дюйма) от левого верхнего угла его видимой части, а не экрана, - и только
	// строки целиком, даже для одного символа (флажок стоял на меню, форум 07.10). Каретка там: её строка, перенесённая на
	// экран, а в строке - номер символа, умноженный на ширину символа (шрифт редактора моноширинный, Lucida Console).
	// Редактор, который отдаёт координаты экрана (первая видимая строка - там, где он сам на экране), - как обычно.
	// 1 - каретка в rc; 0 - не этот редактор или координаты экрана (пустой текст - тоже: строк не видно) - как обычно;
	// -1 - этот, в своих координатах, а каретку не найти: как обычно нельзя - его координаты сочлись бы за экранные
	// (флаг у угла экрана).
	static int EditorCaret(IUIAutomationElement* el, IUIAutomationTextPattern* tp, IUIAutomationTextRange* caret, RECT& rc) {
		CComBSTR cls;
		if (FAILED(el->get_CurrentClassName(&cls)) || !cls || wcscmp(cls, L"WpfTextView") != 0) return 0;
		RECT box{}, first{}, line{};
		CComPtr<IUIAutomationTextRangeArray> visible;
		CComPtr<IUIAutomationTextRange> top;
		int n = 0;
		if (FAILED(el->get_CurrentBoundingRectangle(&box)) || box.right <= box.left ||
			FAILED(tp->GetVisibleRanges(&visible)) || !visible || FAILED(visible->get_Length(&n)) || n < 1 ||
			FAILED(visible->GetElement(0, &top)) || !top || !Rects(top, first)) {
			return 0;
		}
		// Первая видимая строка: у своих координат - у нуля (сверху бывает видна только её часть), у экранных - у края
		// редактора. Прокрутка вбок сдвигает строки влево.
		const LONG h = first.bottom - first.top;
		const bool own = first.left <= 8 && std::abs(first.top) <= h + 2;
		const bool screen = std::abs(first.left - box.left) <= 8 && std::abs(first.top - box.top) <= h + 2;
		if (!own || screen) return 0;

		// Строка каретки и сколько символов в ней до каретки. У пустой строки прямоугольника нет (s1n, форум 08.10: флаг
		// на пустой строке стоял у угла экрана; проверка - tools\test_ise, ISE на скрытом рабочем столе): посреди текста
		// он есть у её перевода строки - символа после каретки, в конце текста - строка под предыдущей.
		CComPtr<IUIAutomationTextRange> ln, head, doc;
		CComBSTR text, before;
		auto emptyLine = [&] {
			RECT r{};
			CComPtr<IUIAutomationTextRange> ch, prev;
			if (SUCCEEDED(caret->Clone(&ch)) && ch && SUCCEEDED(ch->ExpandToEnclosingUnit(TextUnit_Character)) && Rects(ch, r)) {
				line = r;
				return true;
			}
			int moved = 0; // ISE отвечает 1 и на шаг назад
			if (SUCCEEDED(ln->Clone(&prev)) && prev && SUCCEEDED(prev->Move(TextUnit_Line, -1, &moved)) && moved != 0 &&
				SUCCEEDED(prev->ExpandToEnclosingUnit(TextUnit_Line)) && Rects(prev, r)) {
				line = { r.left, r.bottom, r.right, r.bottom + (r.bottom - r.top) };
				return true;
			}
			return false;
		};
		if (FAILED(caret->Clone(&ln)) || !ln || FAILED(ln->ExpandToEnclosingUnit(TextUnit_Line)) ||
			!(Rects(ln, line) || emptyLine()) || FAILED(ln->Clone(&head)) || !head ||
			FAILED(head->MoveEndpointByRange(TextPatternRangeEndpoint_End, caret, TextPatternRangeEndpoint_Start)) ||
			FAILED(ln->GetText(-1, &text)) || FAILED(head->GetText(-1, &before))) {
			return -1;
		}
		auto chars = [](const CComBSTR& s) {
			UINT len = s.Length();
			while (len > 0 && (s[len - 1] == L'\r' || s[len - 1] == L'\n')) len--;
			return len;
		};
		const UINT len = chars(text), col = std::min(chars(before), len);
		// Прямоугольник строки с переводом строки в конце шире на символ; у последней строки текста его нет.
		int end = 0;
		const bool last = SUCCEEDED(tp->get_DocumentRange(&doc)) && doc &&
			SUCCEEDED(ln->CompareEndpoints(TextPatternRangeEndpoint_End, doc, TextPatternRangeEndpoint_End, &end)) && end >= 0;
		const UINT cells = len + (last ? 0 : 1);
		const double x = line.left + (cells ? double(line.right - line.left) * col / cells : 0);
		UINT dpiX = 96, dpiY = 96;
		GetDpiForMonitor(MonitorFromRect(&box, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
		const double k = dpiX / 96.0;
		const LONG cx = box.left + std::lround(x * k);
		rc = { cx, box.top + std::lround(line.top * k), cx + 1, box.top + std::lround(line.bottom * k) };
		return rc.bottom > rc.top ? 1 : -1;
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

		if (range && tp) {
			const int editor = EditorCaret(el, tp, range, rc);
			if (editor != 0) return editor > 0;
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
		// Высота строки: размер шрифта поля (UI Automation) с межстрочным, нет его - 20 точек при 96 dpi.
		UINT dpiX = 96, dpiY = 96;
		GetDpiForMonitor(MonitorFromRect(&box, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
		LONG line = MulDiv(20, dpiY, 96);
		VARIANT size;
		VariantInit(&size);
		if (doc && SUCCEEDED(doc->GetAttributeValue(UIA_FontSizeAttributeId, &size)) && size.vt == VT_R8 && size.dblVal > 4 &&
			size.dblVal < 100)
			line = std::lround(size.dblVal * dpiY / 72.0 * 1.3);
		VariantClear(&size);
		const LONG h = box.bottom - box.top;
		// Поле в одну строку (адрес и тема в eM Client): текст посередине. Многострочное - выше трёх строк (WeChat - около
		// семи): однострочное с отступами (40 точек при 10,5 пт) бывает выше двух, и флаг лёг бы на текст.
		if (h <= 3 * line) {
			const LONG pad = std::max<LONG>(2, h / 6);
			rc = { box.left + pad, box.top + pad, box.left + pad + 1, box.bottom - pad };
		}
		else { // в несколько строк (WeChat): первая строка сверху - а не всё поле, флажок вставал под его низ
			const LONG pad = MulDiv(3, dpiX, 96); // текст в WeChat - в 2-3 точках от края поля
			rc = { box.left + pad, box.top + pad, box.left + pad + 1, box.top + pad + line };
		}
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
		m_excludedPid = 0; // список "Не работать в приложениях" мог смениться
		m_report.clear();
		{
			auto cfg = conf_get_unsafe();
			const auto folder = PathUtils::GetPath_folder_noLower2() / L"Flags" / StrUtils::Convert(cfg->caret_flag_set);
			LOG_ANY("caret flag: settings - {} (caret_flag {}), look {}{}, size {}, place {}, opacity {}",
				cfg->caret_flag == 1 ? "always" : cfg->caret_flag == 2 ? "for a moment" : "off", cfg->caret_flag,
				cfg->caret_flag_set, LetterIcons::Is(cfg->caret_flag_set) || std::filesystem::is_directory(folder) ? "" : " (no such folder)",
				cfg->caret_flag_size, cfg->caret_flag_place, cfg->caret_flag_opacity);
		}
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
	std::string m_lastDetail; // браузер: что нашли в прошлый раз (для журнала)

	// Программа впереди - из "Не работать в приложениях" (disableInPrograms) или удалённый рабочий стол, как у
	// IsSkipProgramTop (Settings.h): там FluentSwitcher молчит, и флажка нет (форум, 08.10.2026: "значок у курсора в
	// этих программах все равно продолжает отображаться"). Процесс - тот же, что смотрит движок: окна в фокусе
	// (GetFocusedWndInfo), не окна впереди - у приложений из Магазина (Калькулятор) впереди рамка ApplicationFrameHost.exe,
	// а пишут в окно самого приложения. Ответ - на процесс, до смены настроек (Refresh).
	DWORD m_excludedPid = 0;
	HWND m_excludedFg = nullptr; // окно впереди, для которого спрашивали (Where)
	bool m_excluded = false;
	std::wstring m_excludedName; // имя exe этого процесса - и для журнала
	bool Excluded(DWORD pid) {
		if (pid != m_excludedPid) {
			m_excludedPid = pid;
			std::wstring path, name;
			auto cfg = conf_get_unsafe();
			m_excluded = Utils::GetProcLowerNameByPid(pid, path, name) == TStatus::SW_ERR_SUCCESS && !name.empty() &&
				(cfg->disableInPrograms.contains(name) || cfg->disableInPrograms.contains(path) || RemoteDesktop::IsClient(name));
			m_excludedName = name.empty() ? L"?" : name;
		}
		return m_excluded;
	}

	bool m_visible = false;
	HWND m_shownFg = nullptr;
	ULONGLONG m_showUntil = 0;   // режим "ненадолго": до этого времени
	HKL m_lay = 0;
	std::wstring m_imgKey;       // какая картинка в окне
	RECT m_bbox{};               // видимая (непрозрачная) часть картинки
	// Журнал отладки: что с флажком сейчас - показан (как нашли каретку) или почему спрятан, с программой впереди. Пишется
	// только при смене, не на каждое движение каретки (форум, 08.10.2026: у gutasiho флажка нет нигде - почему, журнал не
	// говорил).
	std::string m_report;
	// Журнал отладки включён: только тогда собирать строки для Report (флажок обновляется на каждую клавишу).
	static bool Reporting() { return GetLogLevel() >= LOG_LEVEL_2; }
	void Report(const std::string& key, const std::string& detail = {}) {
		if (key == m_report) return;
		m_report = key;
		LOG_ANY("caret flag: {}{}", key, detail);
	}
	// Программа и класс окна впереди - для журнала.
	std::string Where(HWND fg) {
		wchar_t cls[96]{};
		if (fg) GetClassNameW(fg, cls, (int)std::size(cls));
		const std::wstring name = fg == m_excludedFg ? m_excludedName : L"?";
		return std::format(" in {} [{}]", StrUtils::Convert(name), StrUtils::Convert(std::wstring(cls)));
	}
	void HideBecause(const char* why, HWND fg = nullptr) {
		if (Reporting()) Report(std::string("hidden - ") + why + (fg ? Where(fg) : std::string()));
		Hide();
	}

	enum : UINT_PTR { TimerUpdate = 1, TimerBrief = 2, TimerRecheck = 3 };

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
				if (lParam) { // щелчок: показаться (режим "ненадолго") и проверить ещё раз чуть позже
					self->Brief();
					SetTimer(hwnd, TimerRecheck, 350, nullptr);
				}
				self->Poke((UINT)wParam);
				return 0;
			case WM_TIMER:
				KillTimer(hwnd, wParam);
				if (wParam == TimerUpdate || wParam == TimerBrief || wParam == TimerRecheck) self->Update();
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

	// Браузер: окно Chromium - страница или рамка с адресной строкой (Chrome, Brave, Edge, Electron: Chrome_WidgetWin_1;
	// Яндекс: Chrome_Yandex_WidgetWin_1 - поэтому по образцу Chrome_*WidgetWin*) - или Firefox.
	static bool Browser(HWND fg, HWND focus) {
		wchar_t cls[128]{};
		HWND w = focus ? focus : fg;
		if (!w || !GetClassNameW(w, cls, (int)std::size(cls))) return false;
		const std::wstring_view c = cls;
		return c == L"MozillaWindowClass" || c == L"Chrome_RenderWidgetHostHWND" ||
			(c.starts_with(L"Chrome_") && c.find(L"WidgetWin") != std::wstring_view::npos);
	}

	// Программы, где системная каретка есть, но не там (или её нет вовсе): сразу UI Automation. Блокнот (RichEditD2DPT) - не
	// из них: его системная каретка верна, а UI Automation на пустой строке говорит о следующей (Дмитрий 08.10.2026:
	// флажок на строку ниже каретки).
	static bool UiaFirst(HWND fg, HWND focus) {
		return IsClass(fg, { L"ApplicationFrameWindow" }) ||
			IsClass(focus, { L"Windows.UI.Core.CoreWindow", L"Microsoft.UI.Content.DesktopChildSiteBridge",
				L"Windows.UI.Input.InputSite.WindowClass" });
	}

	// Полный экран: почему так решили (для журнала), или nullptr. "Занят" (QUNS_BUSY) Windows не спрашиваем: у gutasiho
	// (форум, 08.10.2026; Windows 10 LTSC и 11) она отвечала так всегда - в Блокноте, Проводнике, Notepad++, - и флажка
	// не было нигде. Окно во весь экран видно и само (ниже), а игры с Direct3D и режим презентации Windows называет прямо.
	static const char* IsFullscreen(HWND fg) {
		QUERY_USER_NOTIFICATION_STATE st{};
		if (SUCCEEDED(SHQueryUserNotificationState(&st))) {
			if (st == QUNS_RUNNING_D3D_FULL_SCREEN) return "full screen: Windows says Direct3D full screen (QUNS_RUNNING_D3D_FULL_SCREEN)";
			if (st == QUNS_PRESENTATION_MODE) return "full screen: Windows says presentation mode (QUNS_PRESENTATION_MODE)";
		}
		if (IsClass(fg, { L"Progman", L"WorkerW" })) return nullptr;
		RECT wr{};
		MONITORINFO mi = { sizeof(mi) };
		if (!GetWindowRect(fg, &wr) || !GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi)) return nullptr;
		const bool covers = wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top && wr.right >= mi.rcMonitor.right &&
			wr.bottom >= mi.rcMonitor.bottom && !(GetWindowLongW(fg, GWL_STYLE) & WS_CAPTION);
		return covers ? "full screen: the window covers the whole screen, no title bar" : nullptr;
	}

	// Системная каретка: клиентские координаты её окна -> экран, как его видит это окно -> физические пиксели.
	// Окно, которое Windows масштабирует само (не знает о DPI), видит экран в своих "логических" точках:
	// переводим по его же прямоугольнику, логическому и физическому (LogicalToPhysicalPointForPerMonitorDPI
	// для такого окна в tools\test_caret.cpp не пересчитал ничего).
	static bool SystemCaret(const GUITHREADINFO& gti, RECT& out) {
		HWND w = gti.hwndCaret;
		// 2x2 - не каретка текста (Telegram ставит такую над строкой, Msaa): дальше - MSAA, UI Automation.
		if (!w || gti.rcCaret.bottom - gti.rcCaret.top < 4) return false;
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
		// Свой набор и своё "не показывать": значок в трее (скрыт, значок приложения) флаг у курсора не прячет (до
		// 07.10.2026 прятал - форум: "отключение значка в трее отключает значок у курсора").
		int mode = cfg->caret_flag;
		if (mode == 0) return HideBecause("off in the settings");
		HWND fg = GetForegroundWindow();
		if (!fg) return HideBecause("no window in front");

		DWORD pid = 0;
		DWORD tid = GetWindowThreadProcessId(fg, &pid);
		if (pid != m_hookedPid) HookProcess(pid);
		const auto focused = Utils::GetFocusedWndInfo();
		m_excludedFg = fg;
		if (Excluded(focused.pid_top ? focused.pid_top : pid))
			return HideBecause("an app of \"Don't work in apps\" or a remote desktop", fg);
		if (const char* full = IsFullscreen(fg)) return HideBecause(full, fg);

		// Раскладка сменилась - в режиме "ненадолго" это повод показаться.
		HKL lay = focused.lay;
		if (lay && lay != m_lay) {
			if (m_lay) Brief();
			m_lay = lay;
		}
		if (mode == 2 && GetTickCount64() > m_showUntil) return HideBecause("for a moment: the time is over", fg);

		GUITHREADINFO gti = { sizeof(gti) };
		if (!GetGUIThreadInfo(tid, &gti)) return HideBecause("no thread info (GetGUIThreadInfo)", fg);
		if (gti.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE | GUI_INMOVESIZE))
			return HideBecause("a menu is open or the window is being moved", fg);

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
		if (!eventDriven && !browser && focus == m_noCaretFocus) return HideBecause("no caret in this window, found before", fg);
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
		if (m_askedBrowser && res.detail != m_lastDetail) {
			LOG_ANY("caret flag: {}", res.detail);
			m_lastDetail = res.detail;
		}
		if (!res.ok) {
			const char* why = *res.how ? res.how : "no caret (system caret, MSAA, UI Automation)";
			if (Reporting()) Report(std::string("hidden - ") + why + Where(m_askedFg));
			// Браузер: поле могло ещё ехать на место (открывается с анимацией), а Chromium - только включать
			// специальные возможности. Ещё две попытки.
			if (m_askedBrowser && m_retry < 2) {
				m_retry++;
				m_retrying = true;
				EventPoke(m_retry == 1 ? 200 : 600);
			}
			Hide();
			return;
		}
		Place(m_askedFg, res.rc, res.how);
	}

	// Картинка флага текущей раскладки нужного размера - в окно (если она другая). Нет - why: почему (для журнала).
	bool PrepareImage(int px, std::string& why) {
		if (!m_lay) {
			why = "the layout is not known";
			return false;
		}
		bool gray = !g_enabled.IsEnabled();
		auto id = Utils::GetNameForHKL_simple(m_lay);
		auto cfg = conf_get_unsafe();
		const int opacity = std::clamp(cfg->caret_flag_opacity, 10, 100);
		auto key = std::format(L"{}|{}|{}|{}|{}|{}", id, px, gray, StrUtils::Convert(cfg->caret_flag_set), cfg->useBritishFlag, opacity);
		if (key == m_imgKey) return true;
		auto img = IconMgr::Inst().GetImage(id.c_str(), px, gray);
		if (!img || !img->IsOk()) {
			why = std::format("no flag picture for {} ({})", StrUtils::Convert(std::wstring(id)), cfg->caret_flag_set);
			return false;
		}

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
			why = "no memory for the picture (CreateDIBSection)";
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
		if (!ok) {
			why = std::format("the flag window did not take the picture (UpdateLayeredWindow, error {})", GetLastError());
			return false;
		}
		m_imgKey = key;
		m_bbox = bbox;
		return true;
	}

	void Place(HWND fg, const RECT& caret, const char* how) {
		// Каретка за пределами окна (прокрутили, устарела) - флажка нет.
		RECT wr{};
		if (!GetWindowRect(fg, &wr) || caret.left < wr.left - 4 || caret.left > wr.right + 4 || caret.top < wr.top - 4 ||
			caret.bottom > wr.bottom + 4) {
			if (Reporting()) Report(std::string("hidden - the caret (") + how + ") is outside its window" + Where(fg),
				std::format(": caret ({},{})-({},{}), window ({},{})-({},{})", caret.left, caret.top, caret.right, caret.bottom,
					wr.left, wr.top, wr.right, wr.bottom));
			return Hide();
		}
		HMONITOR mon = MonitorFromPoint({ caret.left, caret.top }, MONITOR_DEFAULTTONEAREST);
		UINT dpiX = 96, dpiY = 96;
		GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
		int px = MulDiv(std::clamp(conf_get_unsafe()->caret_flag_size, 12, 64), dpiX, 96);
		std::string noPicture;
		if (!PrepareImage(px, noPicture)) {
			if (Reporting()) Report("hidden - " + noPicture + Where(fg));
			return Hide();
		}

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
		if (Reporting()) Report(std::string("shown (") + how + ")" + Where(fg), std::format(": caret ({},{})-({},{}), flag at ({},{}), {} px, visible {}",
			caret.left, caret.top, caret.right, caret.bottom, x, y, px, IsWindowVisible(m_wnd) != FALSE));
		m_visible = true;
		m_shownFg = fg;
		if (conf_get_unsafe()->caret_flag == 2) {
			auto left = m_showUntil > GetTickCount64() ? m_showUntil - GetTickCount64() : 0;
			SetTimer(m_wnd, TimerBrief, (UINT)left + 20, nullptr);
		}
	}
};
