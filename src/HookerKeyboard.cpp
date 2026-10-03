
LRESULT CALLBACK Hooker::HookerKeyboard::LowLevelKeyboardProc(
	_In_  int nCode,
	_In_  WPARAM wParam,
	_In_  LPARAM lParam
) {

	if (nCode != HC_ACTION) {
		return CallNextHookEx(0, nCode, wParam, lParam);
	}

	Message_Hotkey msg_hotkey;
	bool need_disable_event = false;
	bool held_event = false; // придержано (KeyHold.h): в программу не идёт

	auto process = [&]() {

		GETCONF;

		KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lParam;

		TKeyCode vkCode = CHotKey::UnifyBreak((TKeyCode)k->vkCode); // Ctrl+Break приходит как VK_CANCEL
		auto curKeyState = GetKeyState(wParam);
		bool isInjected = TestFlag(k->flags, LLKHF_INJECTED);
		bool is_low_inject = TestFlag(k->flags, LLKHF_LOWER_IL_INJECTED);
		bool isAltDown = TestFlag(k->flags, LLKHF_ALTDOWN);
		bool isSysKey = wParam == WM_SYSKEYDOWN || wParam == WM_SYSKEYUP;
		bool isExtended = TestFlag(k->flags, LLKHF_EXTENDED);
		bool is_pressed = !TestFlag(k->flags, LLKHF_UP);
		auto scan_code = k->scanCode;
		int iscaps = -1;
		bool isDown = curKeyState == KEY_STATE_DOWN;
		if (isDown) iscaps = Utils::IsCapslockEnabled() ? 1 : 0;

		LOG_ANY(
			"KEY_MSG: {}({:x}) {},scan=0x{:x},inject={},low_inject={},altdown={},syskey={},extended={},is_pressed={},flags=0x{:b},caps={}",
			CHotKey::ToString(vkCode),
			vkCode,
			(curKeyState == KEY_STATE_UP ? "UP" : "DOWN"),
			scan_code,
			isInjected,
			is_low_inject,
			isAltDown,
			isSysKey,
			isExtended,
			is_pressed,
			k->flags,
			iscaps
		);

		if (k->dwExtraInfo == c_MyInjectedId) {
			LOG_ANY(L"skip our keys");
			return;
		}

		// "ДВе ЗАглавные": пока движок исправляет слово, нажатия придерживаются и уходят потом (KeyHold.h).
		const bool replayed = k->dwExtraInfo == KeyHold::c_Replayed;
		if (replayed) {
			KeyHold::OnReplayed();
		}
		else if (KeyHold::active) {
			KeyHold::Hold(*k);
			held_event = true;
			return;
		}

		if (k->vkCode > 255) {
			LOG_ANY(L"k->vkCode > 255: {}", k->vkCode);
		}

		if (k->vkCode == 0) {
			LOG_ANY(L"skip vk 0");
			return;
		}

		if (scan_code == 541) {
			LOG_ANY(L"skip bugged lctrl");
			return;
		}

		if (is_low_inject && cfg->SkipLowLevelInjectKeys) {
			LOG_ANY(L"skip low_inject");
			return;
		}

		CHotKey possible_hk_up_current;
		std::swap(possible_hk_up_current, possible_hk_up); // сразу очищаем

		//if (curKeys.Size() == 0) { disable_up = 0; } // все отпущено, ничего запрещать не надо.

		curKeys.BreakDoubleIfClicked(last_mouse_click_time);
		curKeys.Update(vkCode, isDown, isInjected); // сразу обновляем
		const auto& curk = curKeys.GetHk();

		auto request_disable = [&]() {

			if (isDown) {
				if (curk.ValueKey() != vkCode) {
					LOG_ANY("CRITICAL: urk.ValueKey() != vkCode");
				}

				// https://github.com/Aegel5/SimpleSwitcher/issues/97
				// пока исправление не добаляем.
				// 
				//if (Utils::is_in(vkCode, VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL)) {
				//	LOG_ANY(L"Skip disable key={} for shift or ctrl", CHotKey::ToString(vkCode));
				//	return;
				//}


				// Попробуем пока вообще без запрета UP во избежания ошибок.
				// https://github.com/Aegel5/SimpleSwitcher/issues/97
				// disable_up = vkCode; // up тоже будет в будущем запрещать.
			}
			else {
				//disable_up = 0;
			}

			need_disable_event = true;
			LOG_ANY("Key {} was disabled({})", CHotKey::ToString(vkCode), isDown ? "down": "up");
		};

		curKeys.DebugPrint();

		// Ждём отпускания второго нажатия "дважды" (pending_double в Hooker.h).
		if (pending_double_vk != 0) {
			if (isDown) {
				// другая клавиша или автоповтор этой (удерживают) - уже не "дважды"
				LOG_ANY("double {} canceled by {}", pending_double.ToString(), CHotKey::ToString(vkCode));
				pending_double_vk = 0;
			} else if (vkCode == pending_double_vk) {
				pending_double_vk = 0;
				if (last_mouse_click_time > pending_double_time) {
					LOG_ANY("double {} canceled by mouse click", pending_double.ToString());
				} else {
					msg_hotkey.hotkey = pending_double;
					msg_hotkey.hk = pending_double_hk;
				}
			}
		}

		int check_disabled_status = -1;

		auto check_is_our_key = [&check_disabled_status, cfg](const CHotKey& k1, const CHotKey& k2) {
			if (k1.Compare(k2, CHotKey::COMPARE_IGNORE_KEYUP | CHotKey::COMPARE_IGNORE_DOUBLE)) {
				if (check_disabled_status == -1) {
					check_disabled_status = cfg->IsSkipProgramTop() ? 1 : 0;
				}
				return check_disabled_status == 0;
			}
			return false;
			};

		bool key_up_exists = false;
		bool double_exists = false;

		if (curKeyState == KeyState::KEY_STATE_DOWN) {

			// на любое новое нажатие сбрасываем ожидание запрета на UP во избежание ошибок.
			//disable_up = 0;

			if (curk.IsEmpty())
				return;

			/* Новая система работы double (экспериментальное):

			Два режима работы: существует #double хот-кей и не существует.

			1) Не существует: полностью игнорируем всю систему мульти-нажатий.

			2) Существует:
			1 нажатие - запускаем не_double хот-кей если есть с задержкой,
			2/4/6/8... нажатий - запускаем double и отменяем не_double
			3/5/7/9... нажатий - ничего не делаем.

			*/

			// Для простоты сделаем за 2 прохода (в случае если hk найден)

			bool found_hk = false;

			for (const auto& [hk, key] : cfg->All_hot_keys()) {
				if (!check_is_our_key(key, curk)) continue;
				found_hk = true;
				if (key.IsDouble()) double_exists = true;
				if (key.GetKeyup()) key_up_exists = true;
			}

			if (found_hk) {

				for (const auto& [hk, key] : cfg->All_hot_keys()) {
					if (!check_is_our_key(key, curk)) continue;

					if (!key.GetKeyup()) {

						if (!double_exists) {
							// Режим 1: просто запускаем первый попавшийся hk
							msg_hotkey.hotkey = key;
							msg_hotkey.hk = hk;
							break;
						}
						else {
							// Режим 2.
							if (curKeys.IsMultiple()) {
								if ((curKeys.MultipleCnt() & 1) == 0 && key.IsDouble()) {
									// нашли double
									if (key.OnlyMods()) {
										// сработает на отпускание, если до него не будет других клавиш
										pending_double = key;
										pending_double_hk = hk;
										pending_double_vk = vkCode;
										pending_double_time.SetToNow();
										LOG_ANY("double {} waits for the release", key.ToString());
										break;
									}
									msg_hotkey.hotkey = key;
									msg_hotkey.hk = hk;
									break;
								}
							}
							else {
								if (!key.IsDouble()) {
									// нашли не_double
									msg_hotkey.hotkey = key;
									msg_hotkey.hk = hk;
									break;
								}
							}
						}

					}
					else {
						possible_hk_up = key;
						possible_hktype_up = hk;
					}
				}
			}

			if (msg_hotkey.IsEmpty()) {

				// ctrl + alt

				if (curk.Size() == 3
					&& curk.HasKey(VK_LMENU, true)
					&& curk.HasKey(VK_CONTROL, false)
					&& !curk.IsKnownMods(vkCode)
					&& vkCode == curk.ValueKey()
					&& cfg->fixRAlt
					&& cfg->fixRAlt_lay_ != 0
					) {
					LOG_ANY(L"fix ctrl+alt");
					msg_hotkey.hk = hk_Fix_RAlt;
					msg_hotkey.hotkey = curk;
				}
			}

			if (!msg_hotkey.IsEmpty()) {
				possible_hk_up.Clear();  // не поддерживаем одновременно хоткей на up and down (down в приоритете).
				request_disable();
			}

		}

		if (curKeyState == KeyState::KEY_STATE_UP) {
			//if (disable_up == vkCode) {
			//	request_disable();
			//}
			//else {
				// ищем наш хот-кей.
				// даже если нашли, up никогда не запрещаем.
				// Отпускание второго нажатия "дважды" (msg_hotkey уже есть) важнее: не затираем его.
				if (!possible_hk_up_current.IsEmpty() && msg_hotkey.IsEmpty()) {
					msg_hotkey.hotkey = possible_hk_up_current;
					msg_hotkey.hk = possible_hktype_up;
				}
			//}

		}

		if (!msg_hotkey.IsEmpty()) {

			if (curKeys.IsHold()) {
				LOG_ANY("Skip hk because hold");
			}
			else if (msg_hotkey.hotkey.GetKeyup() && last_mouse_click_time > curKeys.StartOfLastHotKey()) {
				// Possible Ctrl+Click in IDE
				LOG_ANY("HotKey {} was canceled by mouse click", msg_hotkey.hotkey.ToString());
			}
			else {
				int delay = 0;
				if (!msg_hotkey.hotkey.IsDouble() && double_exists) {
					delay = cfg->quick_press_ms; // придется подождать.
					msg_hotkey.delayed_from = GetTickCount64();
				}

				LOG_ANY("post {} {}. has_double {}", msg_hotkey.hotkey.ToString(), (int)msg_hotkey.hk, double_exists);
				msg_hotkey.cur_keys_down = curKeys.AllKeys(); // todo curKey_no_disabled?
				if (need_disable_event)
					Utils::RemoveFirst(msg_hotkey.cur_keys_down, vkCode); // удалим то что запретили, так как поднимать их не нужно.
				Worker()->PostMsg(std::move(msg_hotkey), delay);
			}
		}

		else if (
			curKeyState == KeyState::KEY_STATE_DOWN && !curk.IsEmpty()

			// Пропускаем отсылку, если есть UP хот-кей https://github.com/Aegel5/SimpleSwitcher/issues/70
			// (Если есть просто key или key #double, мы и так сюда не попадем)
			&& !key_up_exists

			) {
			// Пробел после слова, которое может быть "ДВух": следующие нажатия придерживаются, пока движок решает.
			bool hold = false;
			if (vkCode == VK_SPACE) {
				hold = KeyHold::CandidateAtSpace() && !replayed && curk.Size() == 1 && cfg->two_caps &&
					g_enabled.IsEnabled() && KeyHold::CanHold();
				if (hold) {
					LOG_ANY("hold: keys wait for the two caps check");
					KeyHold::Start();
				}
			}
			else if ((vkCode == VK_RETURN || vkCode == VK_TAB) && KeyHold::CandidateAtSpace() && !replayed &&
				curk.Size() == 1 && cfg->two_caps && g_enabled.IsEnabled() && KeyHold::CanHold()) {
				// Enter или Tab сразу после "ДВух": они действуют сразу (сообщение уходит, курсор в другое поле), поэтому
				// ждут сами - сначала исправляется слово, потом клавиша уходит в программу вместе с придержанными.
				LOG_ANY("hold: Enter / Tab waits for the two caps check");
				KeyHold::Start();
				KeyHold::Hold(*k);
				held_event = true;
				Worker()->PostMsg(Message_KeyType{ .vkCode = vkCode, .cur_hotKey = curk, .held_end = true });
				return;
			}
			else {
				KeyHold::Track(vkCode, curk.HasMod(VK_SHIFT), iscaps == 1,
					curk.HasMod(VK_CONTROL) || curk.HasMod(VK_MENU) || curk.HasMod(VKE_WIN));
			}
			Worker()->PostMsg(Message_KeyType{
				.vkCode = vkCode,
				.scan_ext = { (TScanCode)scan_code, isExtended },
				.cur_hotKey = curk, // без учета disabled, но не критично
				.is_caps = iscaps == 1,
				.hold = hold,
				});
		}

	};

	process();

	if (held_event) {
		return 1; // придержано: уйдёт, когда движок исправит слово
	}

	CaretFlagPoke(40); // набор и стрелки двигают каретку, Ctrl+Shift меняет раскладку

	if (need_disable_event && (g_enabled.IsEnabled() || msg_hotkey.hk == hk_ToggleEnabled)){
		// делаем вид, что клавиша не была нажата.
		return 1; 
	}

	return CallNextHookEx(0, nCode, wParam, lParam);

}
