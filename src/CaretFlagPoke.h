#pragma once

#include <atomic>
#include <mutex>

// Окно флажка у текстового курсора (CaretFlag.h). Хуки клавиатуры и мыши из своих потоков только
// "толкают" его: что-то могло сдвинуть каретку - проверить через delay мс.

namespace CaretFlagDetails {
	inline std::atomic<HWND> g_wnd{};
	constexpr UINT WM_Poke = WM_APP + 0x51;
	constexpr UINT WM_ProbeDone = WM_APP + 0x52;

	// Системная каретка окна впереди в последний раз, когда нажали кнопку мыши или сменилось окно (CaretMark).
	struct Mark {
		HWND wnd = nullptr;
		RECT rc{};
	};
	inline std::mutex g_markMtx;
	inline Mark g_mark;
}

// brief - повод показаться в режиме "ненадолго" (щелчок мышью: курсор могли поставить в поле).
inline void CaretFlagPoke(UINT delay = 40, bool brief = false) {
	if (HWND wnd = CaretFlagDetails::g_wnd.load(std::memory_order_relaxed)) {
		PostMessage(wnd, CaretFlagDetails::WM_Poke, delay, brief);
	}
}

// Запомнить, где сейчас системная каретка окна впереди: перехват мыши - на нажатии кнопки, до программы (щелчок её ещё
// не сдвинул), флажок - при смене окна. PowerPoint (CaretFlag.h, CaretMoved): каретка с тех пор не сдвинулась - текст
// не редактируют, она стоит там, где была.
inline void CaretMark() {
	HWND fg = GetForegroundWindow();
	GUITHREADINFO gti = { sizeof(gti) };
	if (!fg || !GetGUIThreadInfo(GetWindowThreadProcessId(fg, nullptr), &gti)) return;
	std::lock_guard lock(CaretFlagDetails::g_markMtx);
	CaretFlagDetails::g_mark = { gti.hwndCaret, gti.rcCaret };
}
