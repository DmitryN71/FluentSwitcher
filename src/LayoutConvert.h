#pragma once

// Текст, набранный не в той раскладке, - в другую раскладку, целиком в памяти: те же клавиши, другие буквы.
//
// В какой раскладке набран текст, решается по всей строке сразу, а не по каждому символу: знаки вроде
// запятой есть в обеих раскладках (в английской - клавиша Б, в русской - Shift + /), и посимвольный
// выбор при неверно определённой текущей раскладке превращал "rjv,byfwbz" в "ком,инация".
// Так же делает LangBar++ (github.com/Krot66/LangBarXX): не набирается строка в текущей раскладке -
// значит, она из другой.
//
// Слова, в которых нет ни одного символа той раскладки ("Ыещз" среди английских букв в «NTgthm» и «Ыещз»), общая
// раскладка не трогает. Их переводит ConvertWords - из их собственной раскладки, но только если словарь скажет,
// что это слово набрано не в той раскладке ("ыещз" - не русское слово, "stop" - английское), а "и" остаётся "и".

namespace LayoutConvert {

	inline bool Keep(wchar_t c) {
		return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
	}

	// Сколько символов текста (кроме пробелов и переводов строк) набирается в раскладке.
	inline int Typeable(const std::wstring& text, HKL lay) {
		int n = 0;
		for (wchar_t c : text) {
			if (!Keep(c) && VkKeyScanExW(c, lay) != -1) n++;
		}
		return n;
	}

	// Раскладка, в которой набран текст: та, где набирается больше всего его символов; при равенстве -
	// текущая (строку из цифр и знаков, одинаковых в обеих раскладках, считаем набранной в текущей).
	inline HKL Source(const std::wstring& text, const std::vector<HKL>& layouts, HKL current) {
		HKL best = current;
		int bestCount = current ? Typeable(text, current) : -1;
		for (HKL lay : layouts) {
			if (lay == current) continue;
			int n = Typeable(text, lay);
			if (n > bestCount) {
				best = lay;
				bestCount = n;
			}
		}
		return best;
	}

	// Каждый символ - той же клавишей (с теми же Shift, AltGr) в раскладке `to`. Символы, которых
	// в раскладке `from` нет, и пробелы остаются как есть.
	inline std::wstring Convert(const std::wstring& text, HKL from, HKL to) {
		std::wstring out;
		out.reserve(text.size());
		for (wchar_t c : text) {
			SHORT res = Keep(c) ? -1 : VkKeyScanExW(c, from);
			if (res == -1) {
				out += c;
				continue;
			}
			UINT vk = LOBYTE(res);
			BYTE mods = HIBYTE(res);
			UINT sc = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, from);
			UINT vk2 = MapVirtualKeyExW(sc, MAPVK_VSC_TO_VK, to);
			BYTE state[256] = {};
			if (mods & 1) state[VK_SHIFT] = 0x80;
			if (mods & 2) state[VK_CONTROL] = 0x80;
			if (mods & 4) state[VK_MENU] = 0x80;
			wchar_t buf[8] = {};
			// Флаг 4: не трогать состояние клавиатуры (мёртвые клавиши), Windows 10 1607 и новее.
			int n = vk2 ? ToUnicodeEx(vk2, sc, state, buf, 8, 4, to) : 0;
			if (n > 0) {
				out.append(buf, n);
			}
			else {
				out += c; // мёртвая клавиша или клавиши в той раскладке нет
			}
		}
		return out;
	}

	// Как Convert(text, from, to), но слова (между пробелами и переводами строк), в которых раскладки `from` нет
	// совсем, - из их собственной раскладки `own` в next(own), если wrong(слово, own, перевод, next(own)).
	template <class Next, class Wrong>
	std::wstring ConvertWords(const std::wstring& text, HKL from, HKL to, const std::vector<HKL>& layouts, Next next,
	                          Wrong wrong) {
		std::wstring out;
		out.reserve(text.size());
		size_t i = 0;
		while (i < text.size()) {
			if (Keep(text[i])) {
				out += text[i++];
				continue;
			}
			size_t j = i;
			while (j < text.size() && !Keep(text[j])) j++;
			const std::wstring word = text.substr(i, j - i);
			i = j;
			if (Typeable(word, from) > 0) {
				out += Convert(word, from, to);
				continue;
			}
			const HKL own = Source(word, layouts, from);
			const HKL ownTo = own != from ? next(own) : 0;
			std::wstring conv = ownTo && ownTo != own ? Convert(word, own, ownTo) : word;
			out += conv != word && wrong(word, own, conv, ownTo) ? conv : word;
		}
		return out;
	}
}
