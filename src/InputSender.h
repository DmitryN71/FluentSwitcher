#pragma once

struct InputSender
{
private:
	std::vector<INPUT> list;
public:

	void Send();

	void Clear()
	{
		list.clear();
	}
	void Add(TKeyCode key, KeyState state, TScanCode_Ext scan = {})
	{
		INPUT cur;
		SwZeroMemory(cur);
		cur.type = INPUT_KEYBOARD;

		if (scan.scan != 0) {
			cur.ki.wScan = scan.scan;
			SetFlag(cur.ki.dwFlags, KEYEVENTF_SCANCODE);
			if (scan.is_ext) {
				SetFlag(cur.ki.dwFlags, KEYEVENTF_EXTENDEDKEY);
			}
		}
		else {
			if (key == 0) {
				LOG_WARN(L"try add empty key");
				return;
			}
			cur.ki.wVk = key;
			// Скан-код и флаг extended - как у настоящей клавиши. Без KEYEVENTF_EXTENDEDKEY Windows принимает
			// отпускание VK_RCONTROL за отпускание левого Ctrl: правый остаётся нажатым, и напечатанный следом
			// текст уходит как Ctrl+буквы. То же с правым Alt, Win, стрелками, Insert/Delete, Home/End и т. п.
			if (key < 0x100) {
				UINT sc = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
				if (HIBYTE(sc) != 0xE1) { // Pause (E1 1D 45) одним скан-кодом не описать
					cur.ki.wScan = LOBYTE(sc);
					if (HIBYTE(sc) == 0xE0) {
						SetFlag(cur.ki.dwFlags, KEYEVENTF_EXTENDEDKEY);
					}
				}
			}
		}

		if (state == KEY_STATE_UP)
			SetFlag(cur.ki.dwFlags, KEYEVENTF_KEYUP);

		list.push_back(cur);
	}
	void AddScanCode(const TKeyBaseInfo& key, KeyState keyState = KEY_STATE_DOWN)
	{
		if (key.is_shift) {
			Add(VK_LSHIFT, keyState);
		}

		if (key.scan_code.scan == 0) {
			Add(key.vk_code, keyState);
		}
		else {
			Add(0, keyState, key.scan_code);
		}
	}
	void AddPressVk(TKeyCode vk, int num = 1)	{
		for (int i = 0; i < num; i++) {
			Add(vk, KEY_STATE_DOWN);
			Add(vk, KEY_STATE_UP);
		}
	}
	static void SendVkKey(TKeyCode vk, int num = 1) {
		InputSender is;
		is.AddPressVk(vk, num);
		is.Send();
	}
	void AddDownVk(const CHotKey& key) {
		for (const auto& k : key ) {
			Add(k, KEY_STATE_DOWN);
		}
	}
	void AddUpVk(const CHotKey& key) {
		for (const auto& k : key | std::views::reverse) {
			Add(k, KEY_STATE_UP);
		}
	}
	static void SendWithPause(const CHotKey& key)
	{
		InputSender is;
		is.AddDownVk(key);
		is.Send();
		Sleep(1);
		is.Clear();
		is.AddUpVk(key);
		is.Send();
	}
	void AddPressVk(const CHotKey& key)	{
		if (key.Size() == 0) return;
		AddDownVk(key);
		AddUpVk(key);
	}
	void AddUnicodePress(wchar_t symbol) {
		INPUT cur = {};
		cur.type = INPUT_KEYBOARD;
		cur.ki.wScan = symbol;
		cur.ki.dwFlags = KEYEVENTF_UNICODE;
		list.push_back(cur);
		cur.ki.dwFlags |= KEYEVENTF_KEYUP;
		list.push_back(cur);
	}
	static void SendHotKey(const CHotKey& key) {
		InputSender is;
		is.AddPressVk(key);
		is.Send();
	}
	void AddPressBase(const TKeyBaseInfo& key)
	{
		AddScanCode(key, KEY_STATE_DOWN);
		AddScanCode(key, KEY_STATE_UP);
	}
	// Те же клавиши, но готовыми символами (KEYEVENTF_UNICODE): что даёт каждая клавиша с её Shift в
	// раскладке lay, считаем сами. Программе не нужно ни состояние Shift, ни уже сменённая раскладка.
	// Клавиша без символа уходит клавишей.
	// Что печатает клавиша с её Shift в раскладке lay; пусто - клавиша без символа (Tab, Enter и т. п.).
	static std::wstring KeyText(const TKeyBaseInfo& key, HKL lay, bool is_now_caps) {
		UINT vk = key.scan_code.scan ? key.scan_code.to_vk_or_def(lay, key.vk_code) : key.vk_code;
		UINT sc = key.scan_code.scan ? key.scan_code.scan : MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, lay);
		BYTE state[256] = {};
		if (key.is_shift) state[VK_SHIFT] = 0x80;
		if (is_now_caps) state[VK_CAPITAL] = 0x01;
		wchar_t buf[8] = {};
		// Флаг 4: не трогать состояние клавиатуры (мёртвые клавиши), Windows 10 1607 и новее.
		int n = vk ? ToUnicodeEx(vk, sc, state, buf, 8, 4, lay) : 0;
		if (n <= 0 || buf[0] < L' ') return {};
		return std::wstring(buf, n);
	}

	// delay_ms > 0: по одной клавише, с паузой после каждой (см. retype_delay_ms).
	static void SendKeysAsText(const TKeyRevert& sendData, HKL lay, bool is_now_caps, int delay_ms = 0) {

		InputSender inputSender;

		LOG_ANY("Send {} keys as text, lay {}, is_now_caps: {}, delay {}", sendData.size(), (void*)lay, is_now_caps, delay_ms);

		for (const auto& key : sendData) {
			auto text = KeyText(key, lay, is_now_caps);
			if (text.empty()) {
				inputSender.AddPressBase(key);
			}
			for (wchar_t c : text) {
				inputSender.AddUnicodePress(c);
			}
			if (delay_ms > 0) {
				inputSender.Send();
				inputSender.Clear();
				Sleep(delay_ms);
			}
		}

		inputSender.Send();
	}

	// count нажатий клавиши; delay_ms > 0 - по одному, с паузой после каждого.
	static void SendVkKeyPaced(TKeyCode vk, int count, int delay_ms) {
		if (delay_ms <= 0) {
			SendVkKey(vk, count);
			return;
		}
		for (int i = 0; i < count; i++) {
			SendVkKey(vk);
			Sleep(delay_ms);
		}
	}

	static void SendKeys(const TKeyRevert& sendData, bool is_now_caps) {

		InputSender inputSender;

		LOG_ANY("Send {} keys. is_now_caps: {}", sendData.size(), is_now_caps);

		for (auto key : sendData)
		{
			//if (isCaps) key.revert_shift();
			inputSender.AddPressBase(key);
		}

		inputSender.Send();
	}

};
