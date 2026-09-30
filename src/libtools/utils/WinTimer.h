#pragma once

#include <optional>

// one per thread
class WinTimer {

	int lastTimerId = 1;
	std::vector<std::function<void()>> timerCallbacks;
	std::function<bool(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)> customH;
	// Сообщения с ответом (SendMessage из другой программы): значение - ответ, пусто - не наше.
	std::function<std::optional<LRESULT>(UINT msg, WPARAM wParam, LPARAM lParam)> answerH;
	HWND hwnd = 0;
	inline thread_local static WinTimer* Inst = 0;

	static LRESULT CALLBACK WindowProc2(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
		switch (uMsg) {
		case WM_TIMER:
		{
			UINT_PTR timerId = wParam - 1;
			if (timerId < Inst->timerCallbacks.size()) {
				Inst->timerCallbacks[timerId]();
			}
			return 0;
		}
		}

		if (Inst->answerH) {
			if (auto answer = Inst->answerH(uMsg, wParam, lParam)) {
				return *answer;
			}
		}

		if (Inst->customH && Inst->customH(hwnd, uMsg, wParam, lParam)) {
			return 1;
		}

		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
public:
	HWND GetHandler() { return hwnd; }
	WinTimer() {
		Inst = this;
		hwnd = WinUtils::CreateMsgWin(L"SimpleSwitcher_Timer_001", WindowProc2);
		IFW_LOG(hwnd != 0);
	}
	void CustomHandler(auto&& func) {
		customH = FORWARD(func);
	}
	void AnswerHandler(auto&& func) {
		answerH = FORWARD(func);
	}
	void CycleTimer(auto&& func, int ms) {
		timerCallbacks.push_back(func);
		auto timeId = SetTimer(hwnd, lastTimerId, ms, NULL);
		IFW_LOG(timeId != 0);
		lastTimerId++;
	}
};
