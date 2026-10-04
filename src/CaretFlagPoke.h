#pragma once

#include <atomic>

// Окно флажка у текстового курсора (CaretFlag.h). Хуки клавиатуры и мыши из своих потоков только
// "толкают" его: что-то могло сдвинуть каретку - проверить через delay мс.

namespace CaretFlagDetails {
	inline std::atomic<HWND> g_wnd{};
	constexpr UINT WM_Poke = WM_APP + 0x51;
	constexpr UINT WM_ProbeDone = WM_APP + 0x52;
}

// brief - повод показаться в режиме "ненадолго" (щелчок мышью: курсор могли поставить в поле).
inline void CaretFlagPoke(UINT delay = 40, bool brief = false) {
	if (HWND wnd = CaretFlagDetails::g_wnd.load(std::memory_order_relaxed)) {
		PostMessage(wnd, CaretFlagDetails::WM_Poke, delay, brief);
	}
}
