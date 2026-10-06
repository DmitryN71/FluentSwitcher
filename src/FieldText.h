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
	CComPtr<IUIAutomationTextPattern2> tp2;
	if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&tp2))) && tp2) {
		BOOL active = FALSE;
		tp2->GetCaretRange(&active, &range);
	}
	if (!range) {
		// Без TextPattern2: выделение (пустое - и есть каретка).
		CComPtr<IUIAutomationTextPattern> tp;
		CComPtr<IUIAutomationTextRangeArray> sel;
		int n = 0;
		if (FAILED(el->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&tp))) || !tp || FAILED(tp->GetSelection(&sel)) ||
			!sel || FAILED(sel->get_Length(&n)) || n < 1 || FAILED(sel->GetElement(0, &range)) || !range)
			return false;
	}
	int moved = 0;
	CComBSTR s;
	if (FAILED(range->MoveEndpointByRange(TextPatternRangeEndpoint_Start, range, TextPatternRangeEndpoint_End)) ||
		FAILED(range->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -count, &moved)) ||
		FAILED(range->GetText(-1, &s)))
		return false;
	text.assign(s ? (const wchar_t*)s : L"", s ? s.Length() : 0);
	atStart = moved > -count;
	return true;
}

}
