// Текст поля перед кареткой (UI Automation, а при отсутствии TextPattern - напрямую у окна Scintilla и AkelEdit):
// автопереключению после щелчка - что в поле перед набранным словом (AutoSwitch::StartedAfterBoundary). Проверено
// (tools/test_field_text.cmd): Edit, RichEdit, Scintilla (Notepad++), AkelEdit (AkelPad); поле ответа Claude Desktop
// (Chromium, ProseMirror) - без TextPattern2, текст отдаёт по выделению.
#pragma once

#include <atlbase.h>
#include <UIAutomation.h>
#include <cwchar>
#include <string>
#include <vector>

namespace FieldText {

// Редакторы на Scintilla (Notepad++) не реализуют UIA TextPattern: читаем байты перед кареткой скалярными
// сообщениями (без указателей между процессами).
inline bool BeforeCaretScintilla(HWND hwnd, int count, std::wstring& text, bool& atStart) {
	DWORD_PTR pos = 0, anchor = 0, cp = 0;
	if (!SendMessageTimeoutW(hwnd, 2008 /*SCI_GETCURRENTPOS*/, 0, 0, SMTO_ABORTIFHUNG, 200, &pos) ||
		!SendMessageTimeoutW(hwnd, 2009 /*SCI_GETANCHOR*/, 0, 0, SMTO_ABORTIFHUNG, 200, &anchor) ||
		!SendMessageTimeoutW(hwnd, 2137 /*SCI_GETCODEPAGE*/, 0, 0, SMTO_ABORTIFHUNG, 200, &cp))
		return false;
	const size_t end = pos < anchor ? (size_t)pos : (size_t)anchor;
	if (end == 0 || count <= 0) {
		text.clear();
		atStart = (end == 0);
		return true;
	}
	size_t cur = end;
	for (int steps = 0; cur > 0 && steps < count; steps++) {
		DWORD_PTR prev = 0;
		if (!SendMessageTimeoutW(hwnd, 2417 /*SCI_POSITIONBEFORE*/, cur, 0, SMTO_ABORTIFHUNG, 200, &prev) || prev >= cur)
			return false;
		cur = (size_t)prev;
	}
	if (end - cur > (size_t)count * 4 + 16) return false;
	std::string bytes;
	bytes.reserve(end - cur);
	for (size_t i = cur; i < end; i++) {
		DWORD_PTR ch = 0;
		if (!SendMessageTimeoutW(hwnd, 2007 /*SCI_GETCHARAT*/, i, 0, SMTO_ABORTIFHUNG, 200, &ch)) return false;
		bytes.push_back((char)(ch & 0xFF));
	}
	const UINT codePage = cp ? (UINT)cp : CP_ACP;
	const int wlen = MultiByteToWideChar(codePage, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
	if (wlen <= 0) return false;
	text.resize(wlen);
	MultiByteToWideChar(codePage, 0, bytes.data(), (int)bytes.size(), text.data(), wlen);
	atStart = (cur == 0);
	return true;
}

inline bool ReadEditLine(HWND hwnd, size_t line, size_t take, std::wstring& out) {
	if (take == 0) {
		out.clear();
		return true;
	}
	if (take > 65530) return false;
	// +2: длина в буфере >= 2 и место для завершающего '\r', который AkelEdit дописывает при EM_GETLINE.
	const WORD cap = (WORD)(take + 2);
	std::wstring buf(cap + 1, L'\0');
	*(WORD*)buf.data() = cap;
	DWORD_PTR got = 0;
	if (!SendMessageTimeoutW(hwnd, EM_GETLINE, line, (LPARAM)buf.data(), SMTO_ABORTIFHUNG, 200, &got) || got < take)
		return false;
	out.assign(buf.data(), take);
	return true;
}

// AkelEdit (AkelPad) и классический Edit без UIA TextPattern: только стандартные сообщения < WM_USER, которые
// Windows пересылает между 32/64-битными процессами.
inline bool BeforeCaretEdit(HWND hwnd, int count, std::wstring& text, bool& atStart) {
	DWORD selStart = 0, selEnd = 0;
	DWORD_PTR res = 0;
	if (!SendMessageTimeoutW(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd, SMTO_ABORTIFHUNG, 200, &res))
		return false;
	const size_t pos = selStart < selEnd ? selStart : selEnd;
	if (pos == 0 || count <= 0) {
		text.clear();
		atStart = (pos == 0);
		return true;
	}
	DWORD_PTR line = 0, lineStart = 0;
	if (!SendMessageTimeoutW(hwnd, EM_LINEFROMCHAR, pos, 0, SMTO_ABORTIFHUNG, 200, &line) || line == (DWORD_PTR)-1 ||
		!SendMessageTimeoutW(hwnd, EM_LINEINDEX, line, 0, SMTO_ABORTIFHUNG, 200, &lineStart) || lineStart == (DWORD_PTR)-1 ||
		lineStart > pos)
		return false;
	const size_t col = pos - (size_t)lineStart;
	std::wstring curLine;
	if (!ReadEditLine(hwnd, (size_t)line, col, curLine)) return false;
	if (col >= (size_t)count) {
		text = curLine.substr(col - (size_t)count);
		atStart = (line == 0 && col == (size_t)count);
		return true;
	}
	if (line == 0) {
		text = std::move(curLine);
		atStart = true;
		return true;
	}
	std::wstring buf = std::move(curLine);
	size_t curL = (size_t)line;
	bool reachedStart = false;
	while (curL > 0 && buf.size() < (size_t)count) {
		curL--;
		const size_t need = (size_t)count - buf.size();
		DWORD_PTR idx = 0, len = 0;
		if (!SendMessageTimeoutW(hwnd, EM_LINEINDEX, curL, 0, SMTO_ABORTIFHUNG, 200, &idx) || idx == (DWORD_PTR)-1 ||
			!SendMessageTimeoutW(hwnd, EM_LINELENGTH, idx, 0, SMTO_ABORTIFHUNG, 200, &len) || len > 65530) {
			buf.insert(0, 1, L'\n');
			break;
		}
		std::wstring prev;
		if (len > 0 && !ReadEditLine(hwnd, curL, (size_t)len, prev)) {
			buf.insert(0, 1, L'\n');
			break;
		}
		const size_t takeFromPrev = need - 1;
		if (prev.size() > takeFromPrev) {
			buf = prev.substr(prev.size() - takeFromPrev) + L'\n' + buf;
		} else {
			buf = prev + L'\n' + buf;
			if (curL == 0) reachedStart = true;
		}
	}
	text = std::move(buf);
	atStart = reachedStart;
	return true;
}

inline bool BeforeCaretWindow(HWND hwnd, int count, std::wstring& text, bool& atStart) {
	wchar_t cls[32] = {};
	if (!hwnd || !GetClassNameW(hwnd, cls, (int)std::size(cls))) return false;
	if (wcsncmp(cls, L"Scintilla", 9) == 0) return BeforeCaretScintilla(hwnd, count, text, atStart);
	if (wcsncmp(cls, L"AkelEdit", 8) == 0 || _wcsicmp(cls, L"Edit") == 0)
		return BeforeCaretEdit(hwnd, count, text, atStart);
	return false;
}

// Текст элемента el перед кареткой - до count знаков; atStart - это весь текст до каретки. false - элемент про текст
// не говорит (нет TextPattern, нет каретки).
inline bool BeforeCaret(IUIAutomationElement* el, int count, std::wstring& text, bool& atStart) {
	if (!el) return false;
	CComPtr<IUIAutomationTextRange> range;
	// Сначала выделение: если после каретки выделено подставленное продолжение (адресная строка браузера:
	// "acc[ounts.google.com]", "GJUJL[f]"), текст перед кареткой - перед началом выделения, а не перед его концом.
	CComPtr<IUIAutomationTextPattern> tp;
	CComPtr<IUIAutomationTextRangeArray> sel;
	int n = 0;
	if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&tp))) && tp &&
		SUCCEEDED(tp->GetSelection(&sel)) && sel && SUCCEEDED(sel->get_Length(&n)) && n >= 1)
		sel->GetElement(0, &range);
	if (!range) {
		CComPtr<IUIAutomationTextPattern2> tp2;
		if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&tp2))) && tp2) {
			BOOL active = FALSE;
			tp2->GetCaretRange(&active, &range);
		}
	}
	if (!range) {
		UIA_HWND uiaHwnd = nullptr;
		return SUCCEEDED(el->get_CurrentNativeWindowHandle(&uiaHwnd)) && uiaHwnd &&
			BeforeCaretWindow((HWND)uiaHwnd, count, text, atStart);
	}
	int cmp = 0;
	range->CompareEndpoints(TextPatternRangeEndpoint_Start, range, TextPatternRangeEndpoint_End, &cmp);
	const HRESULT collapse = cmp <= 0
		? range->MoveEndpointByRange(TextPatternRangeEndpoint_End, range, TextPatternRangeEndpoint_Start)
		: range->MoveEndpointByRange(TextPatternRangeEndpoint_Start, range, TextPatternRangeEndpoint_End);
	int moved = 0;
	CComBSTR s;
	if (FAILED(collapse) ||
		FAILED(range->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -count, &moved)) ||
		FAILED(range->GetText(-1, &s)))
		return false;
	text.assign(s ? (const wchar_t*)s : L"", s ? s.Length() : 0);
	atStart = moved > -count;
	return true;
}

// В поле сейчас выделен текст (подставленное продолжение после каретки в адресной строке браузера: "ЦУФЕР[ук]",
// "GJUJL[f]"): первое нажатие Backspace сотрёт это выделение, а не последнюю набранную букву.
inline bool HasSelection(IUIAutomationElement* el, const std::wstring& typed = {}) {
	if (!el) return false;
	CComPtr<IUIAutomationTextPattern> tp;
	CComPtr<IUIAutomationTextRangeArray> sel;
	CComPtr<IUIAutomationTextRange> range;
	int n = 0;
	if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&tp))) && tp) {
		if (SUCCEEDED(tp->GetSelection(&sel)) && sel && SUCCEEDED(sel->get_Length(&n)) && n >= 1 &&
			SUCCEEDED(sel->GetElement(0, &range)) && range) {
			// В Windows 10 conhost.exe (UiaTextRangeBase::CompareEndpoints) для пустого курсора возвращает cmp = -1,
			// хотя выделения нет (GitHub #4); GetText(1) там же честно отдаёт пустую строку, а в реальном выделении
			// всегда есть хотя бы один символ.
			CComBSTR s;
			if (SUCCEEDED(range->GetText(1, &s)) && s && s.Length() > 0) return true;
		}
		return false;
	}
	if (!typed.empty()) {
		CComPtr<IUIAutomationValuePattern> valPat;
		CComBSTR val;
		if (SUCCEEDED(el->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&valPat))) && valPat &&
			SUCCEEDED(valPat->get_CurrentValue(&val)) && val && val.Length() > typed.size() &&
			!iswspace(val[typed.size()]) && _wcsnicmp(val, typed.c_str(), typed.size()) == 0)
			return true;
	}
	return false;
}

}
