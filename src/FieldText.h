// Текст поля перед кареткой (UI Automation): автопереключению после щелчка - что в поле перед набранным словом
// (AutoSwitch::StartedAfterBoundary). Проверено (tools/test_field_text.cmd, 06.10.2026): поле Windows (Edit) и RichEdit;
// поле ответа Claude Desktop (Chromium, ProseMirror) - без TextPattern2, текст отдаёт по выделению.
#pragma once

#include <atlbase.h>
#include <UIAutomation.h>
#include <string>

namespace FieldText {

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
	if (!range) return false;
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
			int cmp = 0;
			if (SUCCEEDED(range->CompareEndpoints(TextPatternRangeEndpoint_Start, range, TextPatternRangeEndpoint_End, &cmp)) &&
				cmp != 0)
				return true;
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
