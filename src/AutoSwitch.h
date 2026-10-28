// Автопереключение раскладки (autoswitch): слово, набранное не в той раскладке, исправляется само в конце слова -
// на пробеле, Enter или Tab, - а когда можно, то и раньше, посреди слова (autoswitch_early, DecideEarly ниже). Правило
// и исключения - как в LangBar++ (Krot66, github.com/Krot66/LangBarXX, LGPL-3.0), но по словарям Windows
// (WinDictionary.h) и встроенным спискам частых слов (WordStart.h). В конце слова:
//   - набранное - не слово своего языка, а те же клавиши в другой включённой раскладке - слово её языка;
//   - не трогаются: одна буква; слова с цифрами; аббревиатуры (все буквы заглавные - набранные или в другой
//     раскладке); слово без гласных в другой раскладке (сокращение: "ru" - не "кг"); три одинаковых знака в начале; в
//     другой раскладке не одно слово (знак внутри - адрес, почта, путь); свои исключения (в любой из двух форм: cv или
//     см); после щелчка или стрелок в том же окне - слова короче четырёх букв (могли дописывать середину слова), если
//     поле не говорит, что перед словом не буква (StartedAfterBoundary).
//   - опечатка - не другая раскладка: "helo" (в русской раскладке "руды") - словарь знает "hello", на одну букву
//     иначе (слова латиницей от четырёх букв); буквы, ставшие там знаками, какими слова не начинаются и не кончаются
//     ("бувы" - ",eds", "дувх" - "led[");
//   - а опечатка в другой раскладке - переключается: "нфдлштп" - "yalking" (словарь предлагает talking), если там от
//     пяти букв, а набранное ни на одно слово своего языка не похоже (TypoThere);
//   - знак после настоящего слова - просто знак: "it." - не "шею", хотя точка там, где в русской раскладке "ю"; и
//     после короткого незнакомого: "En." - не "Утю" (буква там только из знака на конце - не меньше трёх букв).
//   - слово после ручной смены раскладки, в поле пароля, в консоли - не трогает движок; после Backspace - если он
//     стёр больше этого слова или то, что движок в нём уже переключил; буфер пуст (стёрто не набранное здесь: адрес в
//     браузере) - как после щелчка, кроме стирания сразу после переключения на Enter (WorkerImplement, KEYTYPE_BACKSPACE).
//   - слова из списка "Переключать всегда" (autoswitch_force, в нужном виде: the, a) переключаются и без словаря, и
//     одной буквой: "еру" (для словаря - русское слово) станет the, "ф" - a.
//   - слово с Ы, Ь в начале, где словарь там его не знает, - и так, если только это не опечатка своего (NotRussian):
//     "Ыфин" - Saby;
//   - и слово, где словарь там его не знает, но в набранном много сочетаний букв, каких в своём языке почти нет, а там -
//     мало (Implausible): "ФЗЛЬшккщк" - APKMirror, "Вшфвщс" - Diadoc;
//   - слово через дефис, у которого не в той раскладке только часть после дефиса, - она одна (HyphenTail):
//     "legacy-yfcnhjqrb" - "legacy-настройки".
// Знаки по краям ("vs/" -> "мы.", "[jxe" -> "хочу") не мешают: проверяется буквенная часть, а перепечатываются
// все клавиши слова.
// Проверено на текстах проекта и README LangBar++ (tools/test_autoswitch.cmd, 04.10.2026): из 15 тысяч слов, набранных
// правильно, не переключено ни одно (кроме нарочно набранных не в той раскладке примеров); набранные не в той
// раскладке переключаются в 75-90 % (остальное - названия, сокращения, код); опечаток переключается около 0,3 %.
// Только правило, без клавиатуры; переключает движок (WorkerImplement::AutoSwitchLastWord).
#pragma once

#include "TwoCaps.h"
#include "WinDictionary.h"
#include "ShortWords.h"
#include "WordStart.h"

#include <algorithm>
#include <string>
#include <vector>

namespace AutoSwitch {

// Буквенная часть: знаки по краям отброшены (begin, end - где она в тексте). inner - внутри остались не только буквы.
struct Part {
	std::wstring core;
	bool inner = false;
	size_t begin = 0, end = 0;
};

inline Part Letters(const std::wstring& text) {
	size_t b = 0, e = text.size();
	while (b < e && !TwoCaps::IsLetter(text[b])) b++;
	while (e > b && !TwoCaps::IsLetter(text[e - 1])) e--;
	Part part{ text.substr(b, e - b), false, b, e };
	for (wchar_t c : part.core)
		if (!TwoCaps::IsLetter(c)) part.inner = true;
	return part;
}

// Знаки внутри буквенной части - только дефисы, и между ними буквы ("кто-то", "во-первых", "e-mail").
inline bool OnlyHyphens(const std::wstring& core) {
	if (core.empty()) return false;
	bool hyphen = false;
	for (size_t i = 0; i < core.size(); i++) {
		if (TwoCaps::IsLetter(core[i])) continue;
		if (core[i] != L'-' || i == 0 || core[i - 1] == L'-') return false;
		hyphen = true;
	}
	return hyphen;
}

// Все части слова через дефис - от двух букв: однобуквенную часть словарь пропустит как букву ("`-Verb" - "ё-Муки").
inline bool LongParts(const std::wstring& core) {
	size_t run = 0;
	for (size_t i = 0; i <= core.size(); i++) {
		if (i == core.size() || core[i] == L'-') {
			if (run < 2) return false;
			run = 0;
		}
		else
			run++;
	}
	return true;
}

// Все части слова через дефис - не длиннее трёх букв ("bp-pf").
inline bool ShortParts(const std::wstring& core) {
	size_t run = 0;
	for (wchar_t c : core) {
		run = c == L'-' ? 0 : run + 1;
		if (run > 3) return false;
	}
	return true;
}

// Знаки внутри слова там - только дефисы, и это те же клавиши, что в набранном ("у-Куышвутсн" - "e-Residency").
inline bool HyphensKept(const std::wstring& typed, const std::wstring& there, const Part& a) {
	if (!OnlyHyphens(a.core) || typed.size() != there.size()) return false;
	for (size_t i = a.begin; i < a.end; i++)
		if (there[i] == L'-' && typed[i] != L'-') return false;
	return true;
}

// Слово через дефис в другой раскладке, и дефисы - те же клавиши ("Dj-gthds[" - "Во-первых,"): одно слово для словаря.
// С частью в одну букву ("у-ьфшд" - "e-mail", "f-kz" - "а-ля", "d-nhtnmb[" - "в-третьих"; форум, WinnyS, 11.10.2026) -
// если такая часть и набрана буквой (не знаком: "`-Verb" - "ё-Муки"), часть от двух букв и всего от трёх: словарь
// проверяет такие слова по частям, и "a-b", "j-j", "т-ы" для него - слова.
inline bool SameHyphens(const std::wstring& typed, const std::wstring& there, const Part& a) {
	if (!HyphensKept(typed, there, a)) return false;
	if (LongParts(a.core)) return true;
	size_t run = 0, longest = 0, letters = 0;
	for (size_t i = a.begin; i <= a.end; i++) {
		if (i == a.end || there[i] == L'-') {
			if (run == 1 && !TwoCaps::IsLetter(typed[i - 1])) return false;
			run = 0;
			continue;
		}
		letters++;
		if (++run > longest) longest = run;
	}
	return longest >= 2 && letters >= 3;
}

inline std::wstring Lower(std::wstring s) {
	for (auto& c : s) c = TwoCaps::ToLower(c);
	return s;
}

inline bool AllUpper(const std::wstring& s) {
	if (s.size() < 2) return false;
	for (wchar_t c : s)
		if (!TwoCaps::IsUpper(c)) return false;
	return true;
}

// Слово ПРОПИСНЫМИ от четырёх букв - не аббревиатура (форум, gutasiho, 11.10.2026: "ЗИМА DJPVTPLBZ" в имени файла не
// переключалось - Skip считал аббревиатурой всё прописное): решать по нему, как по слову с заглавной (Titled, "Djpvtplbz" -
// "Возмездия": так словарь знает и имена). Короче - аббревиатура (USB, ООН), не трогаем. Было от пяти; от четырёх - в 1.5.7
// ("PBVF" - "ЗИМА", gutasiho, 12.10): аббревиатура в своей раскладке ("NASA", "JSON") там - не слово ("ТФЫФ"), а без
// гласных (ВДНХ) Skip и так не трогает.
inline constexpr size_t kCapsWordMin = 4;
inline bool CapsWord(const std::wstring& typed, const std::wstring& there) {
	const Part t = Letters(typed), a = Letters(there);
	auto caps = [](const std::wstring& core) {
		size_t letters = 0;
		for (wchar_t c : core) {
			if (c == L'-') continue;
			if (!TwoCaps::IsUpper(c)) return false;
			letters++;
		}
		return letters >= kCapsWordMin;
	};
	return caps(t.core) && caps(a.core);
}
// Первая буква - заглавная, остальные - строчные; знаки как были.
inline std::wstring Titled(std::wstring s) {
	bool first = true;
	for (auto& c : s) {
		if (!TwoCaps::IsLetter(c)) continue;
		if (!first) c = TwoCaps::ToLower(c);
		first = false;
	}
	return s;
}

// Гласная (латиница - и буквы с диакритикой, кириллица - и украинские, белорусские). Другие алфавиты - не знаем: да.
inline bool IsVowel(wchar_t c) {
	const wchar_t l = TwoCaps::ToLower(c);
	switch (TwoCaps::ScriptOf(c)) {
	case TwoCaps::Script::Latin: return wcschr(L"aeiouy", l) != nullptr || l >= 0xC0;
	case TwoCaps::Script::Cyrillic: return wcschr(L"аеёиоуыэюяіїєў", l) != nullptr;
	default: return true;
	}
}

// Знаки, с которых слово в тексте может начинаться (кавычка, скобка) и какими кончаться (знаки препинания, кавычка,
// скобка).
inline constexpr const wchar_t* kLeadSigns = L"'\"(«";
inline constexpr const wchar_t* kTrailSigns = L".,;:!?'\")»";

// Слово из списка со знаками по краям ("C#", ".NET", "Mr.") сравнивается целиком, со знаками: от "C#" без знаков
// осталось бы "c", и под него попадал бы каждый предлог "с" (в "Переключать всегда" - переключался бы в "c").
// ("C++" в набранном не встретится: "+" одинаков в раскладках и делит слова - AnalyzeTyped.)
inline bool Signed(const std::wstring& entry) {
	const Part p = Letters(entry);
	return !p.core.empty() && (p.begin > 0 || p.end < entry.size());
}
// ... набранное - это слово, в кавычках или скобках и со знаками препинания после него ("C#,", "\"C#\"", "Mr.,"):
// знаки снаружи снимаются по одному, пока не совпадёт. Точка в конце записи не обязательна: "etc." - и для "etc".
// Где она в word: [begin, end); full - с точкой записи (без неё - "etc" для "etc.").
struct SignedSpan {
	bool found = false, full = false;
	size_t begin = 0, end = 0;
};
inline SignedSpan FindSigned(const std::wstring& entry, const std::wstring& word) {
	const std::wstring el = Lower(entry);
	const std::wstring bare = el.size() > 1 && el.back() == L'.' ? el.substr(0, el.size() - 1) : std::wstring();
	const std::wstring w = Lower(word);
	for (size_t b = 0;; b++) {
		std::wstring rest = w.substr(b);
		while (true) {
			if (rest == el) return { true, true, b, b + rest.size() };
			if (!bare.empty() && rest == bare) return { true, false, b, b + rest.size() };
			if (rest.empty() || !wcschr(kTrailSigns, rest.back())) break;
			rest.pop_back();
		}
		if (b >= w.size() || !wcschr(kLeadSigns, w[b])) return {};
	}
}
inline bool SameSigned(const std::wstring& entry, const std::wstring& word) {
	return FindSigned(entry, word).found;
}

// Слово из исключений - в любой из двух форм (cv или см).
inline bool Excepted(const std::wstring& typed, const std::wstring& there, const std::vector<std::wstring>& exceptions) {
	const auto tl = Lower(Letters(typed).core), al = Lower(Letters(there).core);
	for (const auto& e : exceptions) {
		if (Signed(e)) {
			if (SameSigned(e, typed) || SameSigned(e, there)) return true;
			continue;
		}
		const auto el = Lower(Letters(e).core);
		if (!el.empty() && (el == tl || el == al)) return true;
	}
	return false;
}

// Слово из списка "Переключать всегда" (в том виде, какой нужен: the, a): там - оно, а набрано не оно (набранное
// правильно не трогаем). Через дефис - тоже, и с частью в одну букву ("e-Residency", "ВСХВ-ВДНХ-ВВЦ"): словарь его
// не проверяет (Дмитрий и Maz на форуме, 10.10).
inline bool Forced(const std::wstring& typed, const std::wstring& there, const std::vector<std::wstring>& forced) {
	const Part t = Letters(typed), a = Letters(there);
	if (a.core.empty() || (a.inner && !HyphensKept(typed, there, a))) return false;
	const auto tl = Lower(t.core), al = Lower(a.core);
	for (const auto& f : forced) {
		if (Signed(f)) {
			if (SameSigned(f, there) && !SameSigned(f, typed)) return true;
			continue;
		}
		const auto fl = Lower(Letters(f).core);
		if (!fl.empty() && fl == al && fl != tl) return true;
	}
	return false;
}

// Одно слово - один список (WinnyS на форуме, 26.10.2026: слово в "Не переключать" и в "Переключать всегда" - что
// главнее? "Не переключать", а запись в другом мертва): исключение закрывает запись entry "Переключать всегда", если не
// пропустит её клавиш keys - самой записи, набранной в другой раскладке. "ещё" там "to`": запись "to" - клавиши "ещ",
// исключение "ещё" их не закрывает, "ещ" по-прежнему станет "to". Так решают и движок (ForcedCoveredBy), и окно настроек.
inline bool Covers(const std::wstring& exception, const std::wstring& entry, const std::wstring& keys) {
	return Excepted(keys, entry, { exception });
}

// Записи "Переключать всегда", по которым переключено typed - there, которые закроет исключение word, когда слово уйдёт в
// "Не переключать" (они уйдут оттуда). Клавиши записи - буквы того, что там; со знаками по краям - вся запись ("4pda",
// ".NET"), а совпала она без своей точки ("уес" - "etc" по "etc.") - её клавиш не набрано, исключение их не закроет:
// "уесю" по-прежнему станет "etc.".
inline std::vector<std::wstring> ForcedCoveredBy(const std::wstring& typed, const std::wstring& there, const std::wstring& word,
                                                 const std::vector<std::wstring>& forced) {
	std::vector<std::wstring> covered;
	if (typed.size() != there.size()) return covered; // клавиша - знак (TailText): иначе клавиш записи не найти
	const Part a = Letters(there);
	for (const auto& f : forced) {
		if (!Forced(typed, there, { f })) continue;
		size_t b = a.begin, e = a.end;
		if (Signed(f)) {
			const SignedSpan s = FindSigned(f, there);
			if (!s.full) continue;
			b = s.begin;
			e = s.end;
		}
		if (Covers(word, there.substr(b, e - b), typed.substr(b, e - b))) covered.push_back(f);
	}
	return covered;
}

// Число с точками и запятыми, где они набраны клавишами, которые в этой раскладке буквы: "1ю5ю3" - "1.5.3", "1б5" -
// "1,5", и в конце предложения "1ю5ю3ю" - "1.5.3." (форум, Maz, 11.10.2026: точку нажали по-английски, а пишут
// по-русски). Знак между цифрами - хотя бы один; не два подряд. Только так: "1/5/3" в английской раскладке (там -
// "1.5.3") - дата или дробь, его не трогаем.
// Буквы перед числом (форум, gutasiho, 11.10.2026: "м1ю5ю4" Caramba исправляет): номер версии ("м1ю5ю4" - "v1.5.4",
// "мю1ю5ю4" - "v.1.5.4", rc, ver) - исправить и их; русское сокращение ("пю1ю2" - "п.1.2", "рисю3" - "рис.3") - оставить,
// исправить знаки; другие буквы - не трогать ("g.1.2" из "п.1.2" было бы порчей). Тогда хватает и одного знака.
// Сколько первых символов оставить как набрано (0 - исправить всё); -1 - не такое число.
inline int NumberFix(const std::wstring& typed, const std::wstring& there) {
	if (typed.size() != there.size() || there.size() < 2) return -1;
	// Число после p букв: цифры, знаки между ними, знак сразу после букв; хотя бы один знак (без букв - между цифрами).
	auto number = [&](size_t p) {
		size_t signs = 0, between = 0;
		bool digits = false;
		for (size_t i = p; i < there.size(); i++) {
			const wchar_t a = there[i];
			if (iswdigit(a)) {
				if (typed[i] != a) return false;
				digits = true;
				continue;
			}
			// Дефис - тот же в обеих раскладках: минус впереди ("-1ю5ю9ю") или между числами ("1ю5ю8ю-1ю5ю9ю" - диапазон
			// версий; форум, Maz, 14.10.2026). За ним - цифра.
			if (a == L'-' && typed[i] == L'-') {
				const bool lead = i == p && p == 0;
				const bool afterNumber = i > p && (iswdigit(there[i - 1]) || there[i - 1] == L'.' || there[i - 1] == L',');
				if (!(lead || afterNumber) || i + 1 >= there.size() || !iswdigit(there[i + 1])) return false;
				continue;
			}
			if ((a != L'.' && a != L',') || !TwoCaps::IsLetter(typed[i])) return false;
			if (i == p ? p == 0 : !iswdigit(there[i - 1])) return false; // не начинается знаком; два знака подряд - нет
			signs++;
			if (i > p && i + 1 < there.size() && iswdigit(there[i + 1])) between++;
		}
		return digits && (p == 0 ? between > 0 : signs > 0);
	};
	size_t letters = 0; // буквы впереди (здесь; там это и знак: "б" в "табл" - запятая)
	while (letters < there.size() && letters < 5 && TwoCaps::IsLetter(typed[letters]) && !iswdigit(there[letters])) letters++;
	if (letters == 0) return number(0) ? 0 : -1;
	// Сколько из них - слово впереди: сначала длиннее ("пю1" - "п" и знак, а не "пю").
	static const wchar_t* const kVersion[] = { L"v", L"ver", L"rc" };
	static const wchar_t* const kRussian[] = { L"п", L"пп", L"ст", L"рис", L"табл", L"гл", L"ч", L"разд", L"стр",
	                                           L"прил", L"т" };
	for (size_t p = std::min<size_t>(letters, 4); p > 0; p--) {
		const std::wstring t = Lower(typed.substr(0, p)), a = Lower(there.substr(0, p));
		const bool version = std::ranges::all_of(a, [](wchar_t c) { return iswalpha(c) != 0; }) &&
			std::ranges::any_of(kVersion, [&](const wchar_t* v) { return a == v; });
		const bool russian = std::ranges::any_of(kRussian, [&](const wchar_t* r) { return t == r; });
		if ((version || russian) && number(p)) return version ? 0 : (int)p;
	}
	return -1;
}

// Адрес, набранный не в той раскладке (форум, Maz, 14.10.2026: "еюьу" - t.me): там - имя сайта или почта, буквы, цифры и
// дефисы через точки, в конце - известная зона (ya.ru, vk.com, t.me, www.google.com, ivan@mail.ru), а набрано
// кириллицей. Слово со знаком внутри иначе словом не считается (Skip: "not one word in the other layout"). Знак после
// адреса - нет: перепечатка сделала бы из русской запятой "?". Что набранное - не слово своего языка ("июля" там -
// "b.kz"), проверяет движок по словарю.
inline bool Address(const std::wstring& typed, const std::wstring& there) {
	if (typed.size() != there.size() || there.size() < 4) return false;
	if (std::none_of(typed.begin(), typed.end(), [](wchar_t c) { return TwoCaps::ScriptOf(c) == TwoCaps::Script::Cyrillic; }))
		return false;
	static const wchar_t* const kZones[] = { L"ru", L"com", L"me", L"net", L"org", L"io", L"su", L"by", L"ua", L"kz", L"uz",
		L"info", L"biz", L"pro", L"app", L"dev", L"ai", L"tv", L"co", L"cc", L"to", L"gg", L"fm", L"ly", L"xyz", L"online",
		L"site", L"store", L"shop", L"tech", L"top", L"club", L"de", L"uk", L"us", L"eu", L"am", L"ge", L"md", L"kg", L"az",
		L"lv", L"lt", L"ee", L"pl", L"cz", L"fr", L"it", L"es", L"nl", L"jp", L"cn" };
	const std::wstring a = Lower(there);
	auto ascii = [](wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9'); };
	std::wstring host = a;
	if (const size_t at = a.find(L'@'); at != std::wstring::npos) {
		const std::wstring user = a.substr(0, at);
		if (user.empty() || !std::all_of(user.begin(), user.end(), [&](wchar_t c) { return ascii(c) || wcschr(L"._-+", c); }))
			return false;
		host = a.substr(at + 1);
	}
	std::vector<std::wstring> labels;
	for (size_t from = 0;;) {
		const size_t dot = host.find(L'.', from);
		labels.push_back(host.substr(from, dot == std::wstring::npos ? std::wstring::npos : dot - from));
		if (dot == std::wstring::npos) break;
		from = dot + 1;
	}
	if (labels.size() < 2) return false;
	for (const auto& l : labels)
		if (l.empty() || l.front() == L'-' || l.back() == L'-' ||
		    !std::all_of(l.begin(), l.end(), [&](wchar_t c) { return ascii(c) || c == L'-'; }))
			return false;
	return std::ranges::any_of(kZones, [&](const wchar_t* z) { return labels.back() == z; });
}

// Апострофы внутри слова там - английские сокращения: "Шэь" - I'm, "вщтэе" - don't, "Дуеэы" - Let's (Дмитрий,
// 15.10.2026: в английских фразах из корпуса это больше половины оставленных слов). Апостроф - на месте набранной буквы
// (э), между буквами латиницы; слово ли это - решает словарь (I'm, don't он знает целиком).
inline bool Apostrophes(const std::wstring& typed, const std::wstring& there, const Part& a) {
	if (typed.size() != there.size() || a.core.size() < 3) return false;
	bool any = false;
	for (size_t i = a.begin; i < a.end; i++) {
		if (TwoCaps::IsLetter(there[i])) {
			if (TwoCaps::ScriptOf(there[i]) != TwoCaps::Script::Latin) return false;
			continue;
		}
		// a.begin и a.end - буквы: соседи внутри есть.
		if ((there[i] != L'\'' && there[i] != L'’') || !TwoCaps::IsLetter(typed[i]) ||
		    !TwoCaps::IsLetter(there[i - 1]) || !TwoCaps::IsLetter(there[i + 1]))
			return false;
		any = true;
	}
	return any;
}

// ----- Сокращения, которые словарь не решит -----
// Аббревиатуры, слова без гласных, названия клавиш, единицы (Дмитрий, 15.10.2026: "ГЫИ" - USB не переключалось,
// "понимаю, что Caramba пока впереди"): прописные короче четырёх букв и слова без гласных Skip не трогает, а словарь
// Windows прописные принимает любые. Свой список на язык: там - сокращение из списка (kShortEn, kShortRu), набранное - не
// слово своего языка (словарь, и строчными) и не из своих списков. ...Keep - что набирают нарочно и что не
// переводить ("ru" - не "кг", "cv" - не "см", "CIF" - не "США", "DC" - не "вс"). Без цифр: "А4" - формат листа, не F4.
inline constexpr const wchar_t* kShortEn[] = {
	L"usb", L"pdf", L"api", L"crm", L"sku", L"vpn", L"dns", L"mit", L"ui", L"ux", L"url", L"uri", L"html", L"css", L"json",
	L"xml", L"yaml", L"sql", L"http", L"https", L"ftp", L"sftp", L"ssh", L"ssl", L"tls", L"tcp", L"udp", L"ip", L"vps",
	L"cpu", L"gpu", L"ram", L"ssd", L"hdd", L"nvme", L"bios", L"uefi", L"os", L"ios", L"lan", L"wan", L"wifi", L"nfc",
	L"gps", L"seo", L"smm", L"cms", L"sdk", L"ide", L"gui", L"cli", L"pc", L"tv", L"hd", L"led", L"lcd", L"oled", L"rgb",
	L"hdmi", L"dvd", L"faq", L"ceo", L"cfo", L"cto", L"hr", L"pr", L"qa", L"mvp", L"saas", L"ai", L"ml", L"gpt", L"llm",
	L"ocr", L"docx", L"xlsx", L"pptx", L"csv", L"txt", L"jpg", L"jpeg", L"png", L"gif", L"svg", L"wav", L"zip", L"rar",
	L"exe", L"dll", L"msi", L"iso", L"pls", L"plz", L"thx", L"thnx", L"btw", L"tbh", L"idk", L"imo", L"imho", L"lmk",
	L"brb", L"omg", L"msg", L"npm", L"src", L"cmd", L"ls", L"pwd", L"sudo", L"env", L"js", L"ts", L"py", L"cpp", L"php",
	L"db", L"ctrl", L"alt", L"esc", L"del", L"ins", L"pgup", L"pgdn", L"prtsc", L"fn", L"ghz", L"mhz", L"khz", L"kb",
	L"mb", L"gb", L"tb", L"px", L"dpi", L"fps", L"rpm", L"wb", L"ym", L"ozon", L"fbo", L"fbs", L"dbs", L"rfbs", L"sms",
	L"roi", L"oem", L"kpi", L"ctr", L"cpc", L"cpm", L"cpa", L"erp", L"cdn", L"sso", L"otp" };
inline constexpr const wchar_t* kShortEnKeep[] = {
	L"ru", L"en", L"ua", L"uk", L"us", L"eu", L"de", L"fr", L"it", L"ok", L"cv", L"cif", L"fob", L"exw", L"fca", L"ddp",
	L"dap", L"cpt", L"cip", L"cfr", L"dpu", L"acc", L"nw", L"dep", L"yr", L"gn", L"dc", L"rd" };
inline constexpr const wchar_t* kShortRu[] = {
	L"инн", L"ооо", L"оао", L"зао", L"пао", L"ип", L"ндс", L"ндфл", L"усн", L"огрн", L"огрнип", L"кпп", L"бик", L"снилс",
	L"упд", L"ттн", L"эдо", L"дрр", L"фнс", L"пфр", L"сфр", L"фсс", L"мвд", L"гибдд", L"жкх", L"рф", L"ссср", L"сша",
	L"ес", L"еаэс", L"оон", L"мкад", L"тц", L"цб", L"сбп", L"сдэк", L"ржд", L"вуз", L"сми", L"чп", L"тв", L"пк", L"гост",
	L"нк", L"тк", L"лк", L"кп", L"см", L"мм", L"км", L"кг", L"мг", L"шт", L"млн", L"млрд", L"тчк", L"зпт", L"спб", L"пн",
	L"вт", L"чт", L"пт", L"вс", L"кв", L"стр", L"вкл", L"мгц", L"ггц", L"кгц", L"мб", L"гб", L"кб", L"тб" };
inline constexpr const wchar_t* kShortRuKeep[] = { L"рк" };

// Знаки по краям сняты: открывающие в начале, знаки после слова в конце ("«ЗВА»," - ЗВА).
inline std::wstring Bare(const std::wstring& s) {
	size_t b = 0, e = s.size();
	while (b < e && wcschr(kLeadSigns, s[b])) b++;
	while (e > b && wcschr(kTrailSigns, s[e - 1])) e--;
	return s.substr(b, e - b);
}

// В списке языка (keepToo - и в keep); слово - строчными, без знаков по краям.
inline bool InShortList(const std::wstring& word, const std::wstring& language, bool keepToo) {
	if (word.empty() || language.size() < 2) return false;
	auto has = [&](const auto& list) { return std::ranges::any_of(list, [&](const wchar_t* w) { return word == w; }); };
	const std::wstring code = Lower(language.substr(0, 2));
	if (code == L"en") return has(kShortEn) || (keepToo && has(kShortEnKeep));
	if (code == L"ru") return has(kShortRu) || (keepToo && has(kShortRuKeep));
	return false;
}

// Там - сокращение из списка, набранное - ничто своего языка: переключать без словаря ("ГЫИ" - USB, "BYY" - ИНН, "Секд" -
// Ctrl, "зды" - pls). spellTyped - словарь своего языка и для прописных (SpellCheck::CheckAnyCaseEvenCaps).
inline bool KnownShort(const std::wstring& typed, const std::wstring& there, const std::wstring& typedLanguage,
                       const std::wstring& thereLanguage, auto&& spellTyped) {
	if (typed.size() != there.size()) return false;
	for (wchar_t c : there)
		if (iswdigit(c)) return false;
	const std::wstring t = Bare(typed), a = Lower(Bare(there));
	if (t.empty() || !InShortList(a, thereLanguage, false) || InShortList(Lower(t), typedLanguage, true)) return false;
	// Знак после него там - из набранной буквы ("зню" - py.: опечатка в "знаю"): так - от трёх букв ("ЗВАб" - PDF,).
	if (a.size() < 3)
		for (size_t i = 0; i < there.size(); i++)
			if (TwoCaps::IsLetter(typed[i]) && !TwoCaps::IsLetter(there[i])) return false;
	return spellTyped(t) != SpellCheck::Result::Word;
}

// Почему слово не трогаем; nullptr - можно спрашивать словари. typed - как набрано, there - те же клавиши в другой
// раскладке; minLetters - 2, после щелчка или стрелок в том же окне 4 (перед словом в поле не буква - 2).
inline const char* Skip(const std::wstring& typed, const std::wstring& there, size_t minLetters,
                        const std::vector<std::wstring>& exceptions) {
	const Part t = Letters(typed), a = Letters(there);
	if (a.core.empty() || (a.inner && !SameHyphens(typed, there, a) && !Apostrophes(typed, there, a)))
		return "not one word in the other layout";
	if (a.core.size() < minLetters) return "too short";
	if (a.core.size() > 30) return "too long";
	for (wchar_t c : typed)
		if (iswdigit(c)) return "digits";
	const auto script = TwoCaps::ScriptOf(a.core[0]);
	if (script == TwoCaps::Script::Other) return "not letters";
	bool vowel = false;
	for (wchar_t c : a.core) {
		if (c == L'-' || c == L'\'' || c == L'’') continue; // дефис (SameHyphens), апостроф (Apostrophes)
		if (TwoCaps::ScriptOf(c) != script) return "mixed letters";
		vowel = vowel || IsVowel(c);
	}
	if (AllUpper(a.core) || AllUpper(t.core)) return "abbreviation";
	if (!vowel) return "no vowels (an abbreviation)";
	if (typed.size() >= 3 && typed[0] == typed[1] && typed[1] == typed[2]) return "repeated letters";
	// Набранные буквы там стали знаками, какими слова не начинаются и не кончаются ("бувы" - ",eds", "дувх" - "led["):
	// это не слово другой раскладки. В начале можно кавычку и скобку, в конце - знаки после слова ("Руддщю" - "Hello.").
	for (size_t i = 0; i < there.size() && i < typed.size(); i++) {
		if (!TwoCaps::IsLetter(typed[i]) || TwoCaps::IsLetter(there[i])) continue;
		if (i < a.begin && !wcschr(kLeadSigns, there[i])) return "a letter turns into a sign at the start";
		if (i >= a.end && !wcschr(kTrailSigns, there[i])) return "a letter turns into a sign at the end";
	}
	// Буквы там добавились только из знаков на конце набранного: это точка или запятая после слова, а не буква.
	if (!t.inner && !t.core.empty() && a.begin >= t.begin && a.end > t.end && t.core.size() < 3)
		return "a short word with a sign after it";
	if (Excepted(typed, there, exceptions)) return "exception";
	return nullptr;
}

// После щелчка или стрелок в том же окне: слово набрано с начала, а не дописано к середине другого? before - текст поля
// перед кареткой (UI Automation, несколько знаков с запасом), atStart - это весь текст поля до каретки. 1 - перед словом
// не буква и не цифра (пробел, начало строки или поля, знак), 0 - буква или цифра (дописывали середину слова), -1 - не
// узнать: поле кончается не набранным (программа ещё не показала его). Пробел или Enter после слова может уже быть в
// поле - пробелы в конце не в счёт; заглавную в начале предложения программа может поставить сама (eM Client), а знак
// заменить (" - «): буквы сравниваются без регистра, знак - любой не буквой.
inline int StartedAfterBoundary(const std::wstring& before, const std::wstring& typed, bool atStart) {
	auto wordChar = [](wchar_t c) { return TwoCaps::IsLetter(c) || iswdigit(c); };
	size_t e = before.size();
	while (e > 0 && (iswspace(before[e - 1]) || before[e - 1] == L'\xA0')) e--;
	if (typed.empty() || e < typed.size()) return -1;
	const size_t b = e - typed.size();
	for (size_t i = 0; i < typed.size(); i++) {
		const wchar_t t = typed[i], s = before[b + i];
		if (wordChar(t) ? TwoCaps::ToLower(s) != TwoCaps::ToLower(t) : wordChar(s)) return -1;
	}
	if (b == 0) return atStart ? 1 : -1;
	return wordChar(before[b - 1]) ? 0 : 1;
}

// Расстояние между словами не больше одной правки: буква лишняя, пропущена, другая или две соседние переставлены.
inline bool OneEditApart(const std::wstring& x, const std::wstring& y) {
	const std::wstring a = Lower(x), b = Lower(y);
	if (a == b) return true;
	const size_t n = a.size(), m = b.size();
	if (n == m) {
		size_t i = 0;
		while (i < n && a[i] == b[i]) i++;
		if (a.compare(i + 1, std::wstring::npos, b, i + 1, std::wstring::npos) == 0) return true; // другая буква
		return i + 1 < n && a[i] == b[i + 1] && a[i + 1] == b[i] &&
			a.compare(i + 2, std::wstring::npos, b, i + 2, std::wstring::npos) == 0; // переставлены
	}
	if (n + 1 != m && m + 1 != n) return false;
	const std::wstring& s = n < m ? a : b; // короче
	const std::wstring& l = n < m ? b : a;
	size_t i = 0;
	while (i < s.size() && s[i] == l[i]) i++;
	return s.compare(i, std::wstring::npos, l, i + 1, std::wstring::npos) == 0;
}

// Опечатка: словарь предлагает вместо набранного слово на одну правку иначе, с той же первой и последней буквой
// (слова от четырёх букв): опечатки обычно в середине ("helo" - "hello"). Только для набранного латиницей: русская
// "каша" (английское слово в русской раскладке) почти всегда на одну букву от какой-нибудь краткой русской формы
// ("еруку" - There - от "ерку"), и проверка теряла бы их десятками (tools/test_autoswitch.cmd), а английская "каша"
// (русское слово в английской раскладке) на английское слово похожа редко.
// Соседние клавиши латинской раскладки (QWERTY): ряды сдвинуты - клавиша верхнего ряда над двумя клавишами нижнего.
inline bool NearKeys(wchar_t x, wchar_t y) {
	static const wchar_t* const rows[] = { L"qwertyuiop", L"asdfghjkl", L"zxcvbnm" };
	int rx = -1, cx = -1, ry = -1, cy = -1;
	for (int r = 0; r < 3; r++) {
		if (const wchar_t* p = wcschr(rows[r], x); p && x) rx = r, cx = (int)(p - rows[r]);
		if (const wchar_t* p = wcschr(rows[r], y); p && y) ry = r, cy = (int)(p - rows[r]);
	}
	if (rx < 0 || ry < 0) return false;
	if (rx == ry) return cx - cy == 1 || cy - cx == 1;
	if (rx - ry != 1 && ry - rx != 1) return false;
	const int upper = rx < ry ? cx : cy, lower = rx < ry ? cy : cx;
	return lower == upper || lower == upper - 1;
}

// Правка на одну букву (OneEditApart), какую делают пальцы: пропущенная буква и две переставленные - любые; другая
// буква - соседняя клавиша; лишняя - соседняя с буквой рядом или та же ещё раз. Строчными.
inline bool FingerSlip(const std::wstring& typed, const std::wstring& word) {
	const size_t n = typed.size(), m = word.size();
	size_t i = 0;
	while (i < n && i < m && typed[i] == word[i]) i++;
	if (n == m) {
		if (i == n) return true;
		if (typed.compare(i + 1, std::wstring::npos, word, i + 1, std::wstring::npos) == 0)
			return NearKeys(typed[i], word[i]); // другая буква
		return true; // переставлены
	}
	if (n + 1 == m) return true; // пропущена
	const wchar_t extra = typed[i];
	return (i > 0 && (extra == typed[i - 1] || NearKeys(extra, typed[i - 1]))) ||
		(i + 1 < n && (extra == typed[i + 1] || NearKeys(extra, typed[i + 1])));
}

// Похожесть на опечатку - только такая, какую делают пальцы (FingerSlip; Дмитрий, 15.10.2026: частые слова не
// переключались - "jlby" - один, "levf." - думаю, "afqk" - файл, "jabct" - офисе: словарь предлагал слово на одну
// правку, но другая буква в нём - далеко от набранной). Первая буква - та же или соседняя клавиша ("dtyle" - style).
inline bool LooksLikeTypo(const std::wstring& word, const std::vector<std::wstring>& suggestions) {
	if (word.size() < 4 || TwoCaps::ScriptOf(word[0]) != TwoCaps::Script::Latin) return false;
	const std::wstring w = Lower(word);
	for (const auto& s : suggestions) {
		const std::wstring l = Lower(s);
		if (l.empty() || l.back() != w.back() || !OneEditApart(w, l) || !FingerSlip(w, l)) continue;
		if (l.front() == w.front() || (l.size() == w.size() && w.compare(1, std::wstring::npos, l, 1, std::wstring::npos) == 0))
			return true;
	}
	return false;
}

// В другой раскладке - слово с опечаткой ("нфдлштп" - "yalking": словарь предлагает talking, walking) - переключить
// (как есть, с опечаткой: исправлять её - не наше дело). Там от пяти букв, без знаков внутри, и словарь её языка
// предлагает слово на одну правку иначе (OneEditApart); набранное же ни на одно слово своего языка не похоже - его
// словарь на одну правку не предлагает ничего (иначе это, скорее, своя опечатка: "привкт"). typedHasSigns - в
// набранном знак внутри ("ghbdtn,s"): своим словом оно бывает, только если его знает словарь ("let's"). Короче пяти
// букв "каша" на одну правку от какого-нибудь слова бывает часто. Не трогает (tools/test_autoswitch.cmd): буквы там из
// знаков по краям ("\"Next" - "ЭТуче"), заглавные внутри (LShift, RevertText - код).
inline constexpr size_t kTypoThereMin = 5;
inline bool TypoThere(const Part& t, const Part& a, bool typedHasSigns, auto&& spellTyped, auto&& suggestTyped,
                      auto&& suggestThere) {
	if (a.inner || a.core.size() < kTypoThereMin || a.begin != t.begin || a.end != t.end) return false;
	for (size_t i = 1; i < t.core.size(); i++)
		if (TwoCaps::IsUpper(t.core[i])) return false;
	if (typedHasSigns && spellTyped(t.core) == SpellCheck::Result::Word) return false;
	bool close = false;
	for (const auto& s : suggestThere(a.core)) {
		if (OneEditApart(a.core, s)) {
			close = true;
			break;
		}
	}
	if (!close) return false;
	if (!typedHasSigns)
		for (const auto& s : suggestTyped(t.core))
			if (OneEditApart(t.core, s)) return false;
	return true;
}

// ----- Не может быть русским -----
// Набрано в русской раскладке слово с Ы или Ь в начале - так русские слова не начинаются, а это клавиши S, M, - и в
// английской там латиница с гласной: переключать, и когда английский словарь этого слова не знает (Дмитрий, 22.10.2026:
// "Ыфин" - Saby не переключалось; "Ьшкщ" - Miro). Решает, только когда обычное правило отказалось - там не слово
// (WorkerImplement::AutoSwitchLastWord). Буква в букву, от трёх букв; после них там можно знак после слова ("Ыфинб" -
// "Saby,"). Не трогает:
//   - слово словаря и из своих списков - всё набранное, вместе с буквами, что там знаки ("ызнаю" - не по "ызна"); нет
//     словаря - ничего (Unknown - не "не слово": сразу после входа в Windows словаря ещё нет);
//   - повтор в начале ("ыыы", "ыых" - смех), "ыгы", "ыгыгы";
//   - опечатку: первая буква - соседняя клавиша (Ы на S - рядом с Ф, В, Ц, У, Я, Ч; Ь на M - рядом с Т, Б, О, Л) - и так
//     получается слово ("ыунт" - "фунт", "ьуман" - "туман") или, от пяти букв, словарь предлагает слово на одну правку
//     от него с той же первой и последней буквой ("ьулдем" - "будем", Дмитрий 23.10.2026: стало "mekltv"); пропущена
//     первая буква - словарь предлагает набранное с буквой впереди ("ынок" - "рынок", "ыну" - "сыну").
// ПРОПИСНЫЕ аббревиатуры (SKU, SMS, FBO) - по встроенному списку (KnownShort): угадывать их по Ы, Й, Щ (1.5.16-test1)
// значило переключать и опечатки прописными ("ДНЕЬ" - "LYTM"), и "СУДЬЮ" с CapsLock (ревью 25.10.2026).
// spellTyped - словарь своего языка (и для прописных), suggestTyped - его подсказки. Сначала все догадки - словарём
// (быстро), подсказки - потом.
inline bool NotRussian(const std::wstring& typed, const std::wstring& there, const std::wstring& typedLanguage,
                       const std::wstring& thereLanguage, auto&& spellTyped, auto&& suggestTyped) {
	if (typed.size() != there.size() || !ShortWords::Is(typedLanguage, L"ru") || !ShortWords::Is(thereLanguage, L"en"))
		return false;
	for (wchar_t c : typed)
		if (iswdigit(c)) return false;
	const Part t = Letters(typed), a = Letters(there);
	if (t.inner || a.inner || a.core.size() < 3 || a.begin != t.begin || a.end > t.end) return false;
	for (size_t i = a.end; i < t.end; i++)
		if (!wcschr(kTrailSigns, there[i])) return false;
	bool vowel = false;
	for (wchar_t c : a.core) {
		if (TwoCaps::ScriptOf(c) != TwoCaps::Script::Latin) return false;
		vowel = vowel || IsVowel(c);
	}
	const std::wstring l = Lower(t.core);
	if (!vowel || (l[0] != L'ы' && l[0] != L'ь') || l[0] == l[1]) return false;
	if (l == L"ыгы" || l == L"ыгыгы" || InShortList(l, typedLanguage, true)) return false;
	if (spellTyped(t.core) != SpellCheck::Result::NotWord) return false;
	// Соседние клавиши первой буквы. Ряды со знаками (там русские Х Ъ Ж Э Б Ю), нижний сдвинут вправо на полклавиши - как
	// в NearKeys.
	static const wchar_t* const kRows[] = { L"qwertyuiop[]", L"asdfghjkl;'", L"zxcvbnm,." };
	static const wchar_t* const kRussian[] = { L"йцукенгшщзхъ", L"фывапролджэ", L"ячсмитьбю" };
	const wchar_t key = TwoCaps::ToLower(a.core[0]);
	std::vector<std::wstring> guesses;
	for (int r = 0; r < 3; r++) {
		const wchar_t* p = wcschr(kRows[r], key);
		if (!p) continue;
		const int col = (int)(p - kRows[r]);
		for (int q = 0; q < 3; q++)
			for (int c = 0; kRows[q][c]; c++) {
				const bool adjacent = q == r ? (c == col - 1 || c == col + 1)
					: q == r + 1 ? (c == col || c == col - 1) : q == r - 1 ? (c == col || c == col + 1) : false;
				if (!adjacent) continue;
				std::wstring guess = t.core;
				guess[0] = TwoCaps::IsUpper(t.core[0]) ? (wchar_t)(UINT_PTR)CharUpperW((LPWSTR)(UINT_PTR)kRussian[q][c])
				                                      : kRussian[q][c];
				if (spellTyped(guess) != SpellCheck::Result::NotWord) return false;
				guesses.push_back(Lower(guess));
			}
	}
	for (const auto& s : suggestTyped(l)) {
		const std::wstring sl = Lower(s);
		if (sl.size() == l.size() + 1 && sl.compare(1, std::wstring::npos, l) == 0) return false;
	}
	if (l.size() >= kTypoThereMin)
		for (const auto& g : guesses)
			for (const auto& s : suggestTyped(g)) {
				const std::wstring sl = Lower(s);
				if (!sl.empty() && sl.front() == g.front() && sl.back() == g.back() && OneEditApart(g, sl)) return false;
			}
	return true;
}

// ----- Сочетания букв, каких в языке нет -----
// Набранное - не слово своего языка, и там не слово: название, имя, сокращение со словом - "ФЗЛЬшккщк" - APKMirror,
// "ФЗЛЗгку" - APKPure, "Вшфвщс" - Diadoc, "ГтшпуеГШ" - UnigetUI, "Vjcvtnhj" - "Мосметро" (Дмитрий, 28.10.2026: "у Caramba
// Switcher они бы переключились, а у нас нет"). Но в набранном много сочетаний букв, каких в своём языке почти нет ("^фз",
// "ккщ", "кщк"), а там - мало: переключать. Сочетания - по полным спискам слов (WordStart::Combos), с краями слова: пара
// редкая, когда она меньше чем в двух словах списка (и считается за две), тройка - меньше чем в пяти. Переключать, когда
// там таких не больше kImplausibleThere, а в набранном - больше на kImplausibleMargin и больше. Проверено на журнале
// Дмитрия и частотных списках FrequencyWords (28.10.2026): из 18 слов "там не слово" - 15; из 324 тысяч слов, которых
// нет в списках словаря (имена, сленг, опечатки), трогает около 60 - мусор субтитров ("унпньн") и иностранные слова
// латиницей (lgbtq, atque, Yevhen).
// Решает, только когда обычное правило отказалось - там не слово (WorkerImplement::AutoSwitchLastWord). Только между
// кириллицей и латиницей, буква в букву, от пяти букв, без цифр и знаков внутри (знак после слова - можно, как в
// NotRussian). Не трогает:
//   - растянутое ("шшшшш", "сыыыр" - буква трижды подряд) и повторы слогов ("blblbl" - разных букв не больше половины);
//   - опечатку своего: словарь предлагает слово на одну правку ("лучще" - "лучше", "мыщцы" - "мышцы").
// Сленг своего языка ("гифки") не трогается и так: редких сочетаний в нём мало (здесь - три тройки).
// spellTyped - словарь своего языка (и для прописных), suggestTyped - его подсказки.
inline constexpr size_t kImplausibleMin = 5;
inline constexpr int kImplausibleThere = 3, kImplausibleMargin = 4;

// Сколько сочетаний букв слова язык почти не знает (см. выше); -1 - не его буквы или нет списка.
inline int UnusualCombos(const std::wstring& word, const std::wstring& language) {
	const auto* combos = WordStart::Combos(language);
	if (!combos) return -1;
	constexpr size_t n = WordStart::impl::Combos::kLetters;
	std::vector<int> index{ 0 };
	for (wchar_t c : word) {
		const int i = WordStart::impl::LetterIndex(c);
		if (i < 0) return -1;
		index.push_back(i);
	}
	index.push_back(0);
	int unusual = 0;
	for (size_t i = 0; i + 1 < index.size(); i++)
		if (combos->two[index[i] * n + index[i + 1]] < 2) unusual += 2;
	for (size_t i = 0; i + 2 < index.size(); i++)
		if (combos->three[(index[i] * n + index[i + 1]) * n + index[i + 2]] < 5) unusual++;
	return unusual;
}

inline bool Implausible(const std::wstring& typed, const std::wstring& there, const std::wstring& typedLanguage,
                        const std::wstring& thereLanguage, auto&& spellTyped, auto&& suggestTyped) {
	if (typed.size() != there.size()) return false;
	for (wchar_t c : typed)
		if (iswdigit(c)) return false;
	const Part t = Letters(typed), a = Letters(there);
	if (t.inner || a.inner || a.core.size() < kImplausibleMin || a.begin != t.begin || a.end > t.end) return false;
	for (size_t i = a.end; i < t.end; i++)
		if (!wcschr(kTrailSigns, there[i])) return false;
	// Кириллица с одной стороны, латиница - с другой (русская и украинская раскладки - почти одни клавиши).
	const auto script = [](const std::wstring& s) {
		const auto first = TwoCaps::ScriptOf(s[0]);
		for (wchar_t c : s)
			if (TwoCaps::ScriptOf(c) != first) return TwoCaps::Script::Other;
		return first;
	};
	const auto st = script(t.core), sa = script(a.core);
	if (st == TwoCaps::Script::Other || sa == TwoCaps::Script::Other || st == sa) return false;
	const std::wstring l = Lower(t.core);
	// Растянутое и повторы слогов.
	std::wstring letters = l;
	std::ranges::sort(letters);
	if (2 * (size_t)(std::unique(letters.begin(), letters.end()) - letters.begin()) <= l.size()) return false;
	for (size_t i = 2; i < l.size(); i++)
		if (l[i] == l[i - 1] && l[i] == l[i - 2]) return false;
	const int here = UnusualCombos(l, typedLanguage), away = UnusualCombos(Lower(a.core), thereLanguage);
	if (here < 0 || away < 0 || away > kImplausibleThere || here - away < kImplausibleMargin) return false;
	if (spellTyped(t.core) != SpellCheck::Result::NotWord) return false;
	for (const auto& s : suggestTyped(t.core))
		if (OneEditApart(l, Lower(s))) return false;
	return true;
}

// Слова слитно, каждое с заглавной (Дмитрий, 15.10.2026: "СфзыДщсл" - CapsLock, "КгыеВуыл" - RustDesk, "ТгьДщсл" -
// NumLock): словарь знает только части. Две-три части от двух букв, только буквы, каждая - слово (spellThere); новая
// часть - с заглавной после строчной ("camelCase" - тоже; "HTMLParser" - одна часть, не слово).
inline bool CamelWords(const std::wstring& core, auto&& spellThere) {
	std::vector<std::wstring> parts;
	size_t from = 0;
	for (size_t i = 1; i <= core.size(); i++)
		if (i == core.size() || (TwoCaps::IsUpper(core[i]) && !TwoCaps::IsUpper(core[i - 1]))) {
			parts.push_back(core.substr(from, i - from));
			from = i;
		}
	if (parts.size() < 2 || parts.size() > 3) return false;
	for (const auto& p : parts) {
		if (p.size() < 2 || !std::ranges::all_of(p, [](wchar_t c) { return TwoCaps::IsLetter(c); })) return false;
		if (spellThere(p) != SpellCheck::Result::Word) return false;
	}
	return true;
}

// Переключать ли (после Skip). spellTyped / spellThere - словарь языка набранного / другой раскладки
// (SpellCheck::Result по слову). Набранное должно быть точно не словом: со знаком внутри ("j,]`v" - "объём") или из
// одной буквы среди знаков (",s" - "бы") - не слово; иначе спрашиваем словарь (знаки по краям - знаки: "it." - слово
// "it"), нет словаря - не переключаем. Там - слово.
// suggestTyped, suggestThere - подсказки словарей языка набранного и другой раскладки (только когда всё остальное за
// переключение, и там не слово - TypoThere).
// DecideWhy - почему не переключать (для журнала: что пропущено и почему); nullptr - переключать.
// shortTrusted - словарю языка набранного можно верить в коротких словах (ShortWords::TrustDictionary): "Bp-pf" ("Из-за")
// английский словарь принимает по частям (bp, pf - сокращения), и это не в счёт.
inline const char* DecideWhy(const std::wstring& typed, const std::wstring& there, auto&& spellTyped,
                             auto&& spellThere, auto&& suggestTyped, auto&& suggestThere, bool shortTrusted = true) {
	const Part t = Letters(typed), a = Letters(there);
	// Знак внутри набранного - не слово; дефис - может быть и словом ("well-known"): спросить словарь.
	const bool innerSign = t.inner && !OnlyHyphens(t.core);
	// Знак в начале, который там буква (",elm" - "будь", "<jktt" - "Более"): так слова не начинаются - не слово (хотя
	// "elm" без запятой - слово) и не опечатка.
	const bool signFirst = a.begin < t.begin;
	if (!innerSign && !signFirst && !(t.core.size() <= 1 && a.core.size() > t.core.size())) {
		const auto asTyped = spellTyped(t.core);
		if (asTyped == SpellCheck::Result::Unknown) return "no dictionary of the typed language";
		if (asTyped == SpellCheck::Result::Word && !(OnlyHyphens(t.core) && !shortTrusted && ShortParts(t.core)))
			return "a word as typed";
	}
	const auto inOther = spellThere(a.core);
	if (inOther == SpellCheck::Result::Unknown) return "no dictionary of the other layout";
	if (inOther == SpellCheck::Result::NotWord) {
		if (CamelWords(a.core, spellThere)) return nullptr;
		return TypoThere(t, a, innerSign, spellTyped, suggestTyped, suggestThere) ? nullptr : "not a word in the other layout";
	}
	if (!innerSign && !signFirst && LooksLikeTypo(t.core, suggestTyped(t.core))) return "looks like a typo";
	return nullptr;
}
// Буква набранного там - знак после слова ("Руддщю" - "Hello."), а слово проверяется на этой клавише, посреди набора
// (WorkerImplement::AutoSwitchAtSign): так кончается только частое слово. Редкое ("yew", "el") с Ж, Б, Ю на конце (там
// ; , .) - скорее опечатка в русском слове, которое набирают дальше (Дмитрий, 22-23.10.2026: "Нуцж" стало "Yew;", "Удж" -
// "El;", "Яндж" - "Zyl;"). Только простое слово: со знаком внутри (I'm, e-mail) и с заглавной внутри (GitHub) частым оно
// не бывает, а за опечатку его не примешь. Пробел, Enter - не здесь: слово кончилось, решает словарь (ревью 25.10.2026:
// "ПшеРгию" - "GitHub." не переключалось). frequentThere(слово строчными) - частое слово языка другой раскладки (нет
// списков частых для языка - да: не судим).
inline bool RareBeforeSign(const std::wstring& typed, const std::wstring& there, auto&& frequentThere) {
	const Part t = Letters(typed), a = Letters(there);
	if (a.core.empty() || a.end >= t.end || a.inner) return false;
	for (size_t i = 1; i < a.core.size(); i++)
		if (TwoCaps::IsUpper(a.core[i])) return false;
	return !frequentThere(Lower(a.core));
}

// ----- Слово через дефис -----
// Часть слова через дефис ПРОПИСНЫМИ - аббревиатура своего языка из его списков (PHP, API), а там такой нет ("ЗРЗ"): её
// набрали нарочно - всё слово не переводить, только часть после дефиса (HyphenTail): "PHP-yt" - "PHP-не", а не "ЗРЗ-не"
// (словарь там принимает любые прописные; Дмитрий, 23.10.2026). "LYR-ntcn" - "ДНК-тест", "ФШ-зщцукув" - "AI-powered"
// переводятся целиком: там аббревиатура известная.
inline bool OwnAbbreviationPart(const std::wstring& typed, const std::wstring& there, const std::wstring& typedLanguage,
                                const std::wstring& thereLanguage) {
	const Part t = Letters(typed);
	if (typed.size() != there.size() || !OnlyHyphens(t.core)) return false;
	auto known = [](const std::wstring& w, const std::wstring& language) {
		const std::wstring l = Lower(w);
		return InShortList(l, language, true) || ShortWords::Frequent(l, language);
	};
	for (size_t from = t.begin, i = t.begin; i <= t.end; i++)
		if (i == t.end || typed[i] == L'-') {
			const std::wstring part = typed.substr(from, i - from);
			if (AllUpper(part) && known(part, typedLanguage) && !known(there.substr(from, i - from), thereLanguage))
				return true;
			from = i + 1;
		}
	return false;
}

// Слово через дефис, у которого не в той раскладке только часть после последнего дефиса ("legacy-yfcnhjqrb" -
// "legacy-настройки", "PHP-yt" - "PHP-не"; Дмитрий, 25.10.2026): целиком оно не переводится (там "дупфсн-настройки" -
// не слово), часть до дефиса - слово своего языка или из его списков (spellTyped - словарь, и для прописных), а часть
// после - переключилась бы отдельным словом (tailSwitches(набранное, там)). С какого знака переводить; 0 - не такое слово.
inline size_t HyphenTail(const std::wstring& typed, const std::wstring& there, const std::wstring& typedLanguage,
                         auto&& spellTyped, auto&& tailSwitches) {
	const Part t = Letters(typed);
	if (typed.size() != there.size() || !OnlyHyphens(t.core)) return 0;
	const size_t hyphen = t.begin + t.core.rfind(L'-');
	if (there[hyphen] != L'-') return 0;
	const Part head = Letters(typed.substr(0, hyphen));
	if (head.core.size() < 2 || (spellTyped(head.core) != SpellCheck::Result::Word &&
	                             !InShortList(Lower(head.core), typedLanguage, true)))
		return 0;
	return tailSwitches(typed.substr(hyphen + 1), there.substr(hyphen + 1)) ? hyphen + 1 : 0;
}

// Часть после дефиса из двух-трёх букв, которую словарь своего языка пропускает (yt - "не"): там - частое короткое
// слово, набранное - не частое своего ("PHP-yt" - "PHP-не").
inline bool ShortTail(const std::wstring& typed, const std::wstring& there, const std::wstring& typedLanguage,
                      const std::wstring& thereLanguage) {
	const Part t = Letters(typed), a = Letters(there);
	if (t.inner || a.inner || a.core.size() < 2 || a.core.size() > 3 || t.core.size() != a.core.size()) return false;
	return ShortWords::Frequent(Lower(a.core), thereLanguage) && ShortWords::Band(Lower(t.core), typedLanguage) == 0;
}

inline bool Decide(const std::wstring& typed, const std::wstring& there, auto&& spellTyped, auto&& spellThere,
                   auto&& suggestTyped, auto&& suggestThere) {
	return DecideWhy(typed, there, spellTyped, spellThere, suggestTyped, suggestThere) == nullptr;
}

// ----- Посреди слова (autoswitch_early) -----
// Правило LangBar++: после каждой буквы набранное - не слово и не начало слова своего языка, а те же клавиши в другой
// раскладке - начало слова её языка. Начала слов - по встроенным спискам частых слов (WordStart.h); набранное
// проверяется ещё и словарём (слово целиком, подсказки) - редкого слова в списке может не быть.
// Одной буквы мало: опечатка в начале своего слова ("ищт" вместо "ище...") - тоже не начало, а в другой раскладке начало
// бывает часто ("bon"). Поэтому так должно быть и сейчас, и буквой раньше: опечатка со следующей буквой там началом
// остаётся редко, а слово, набранное не в той раскладке, - всегда. С четвёртой буквы: "njkm" - "толь", "штеу" - "inte".
// Хук держит нажатия после такой буквы, пока движок решает (KeyHold.h). В конце слова оно проверяется ещё раз обычным
// правилом.
// Проверено на текстах проекта (tools/test_autoswitch.cmd, 05.10.2026): из 7,7 тысячи слов, набранных правильно,
// посреди слова не переключено ни одно (кроме нарочно испорченных примеров); опечаток переключается 0,3-0,5 % (как в
// конце слова; по одной букве было 3 %); набранные не в той раскладке посреди слова переключаются в 64-78 %, обычно
// на 4-5-й букве (остальное - короткие слова и слова со знаками: их переключает конец слова).
inline constexpr size_t kEarlyMin = 4; // раньше - нет (после щелчка или стрелок в том же окне - с пятой)
inline constexpr size_t kEarlyMax = 8; // длиннее - только в конце слова (KeyHold::EarlyPoint)
// ПРОПИСНЫМИ (с Shift или CapsLock) - с пятой буквы, как то же слово строчными: "PFVJH" - "ЗАМОР…" (Maz, 14.10.2026: в
// строке поиска ждал "ЗАМОРОЗКА", не нажимая пробела). Четыре прописные - ещё аббревиатура (NASA, HTML, ГОСТ): ждать.
inline constexpr size_t kCapsEarlyMin = 5;

enum class Early {
	NotYet, // решать на следующей букве
	Switch, // переключать сейчас
	Never,  // посреди этого слова - нет (решит его конец)
};
struct EarlyVerdict {
	Early what = Early::NotYet;
	const char* why = nullptr;
};

// Исключения и посреди слова: набирают слово из исключений ("mof" - mofii) или слово начинается с исключения от трёх
// букв (так запоминается отменённое переключение посреди слова: "lsh" - и lshift).
inline bool ExceptedEarly(const std::wstring& typed, const std::wstring& there,
                          const std::vector<std::wstring>& exceptions) {
	const std::wstring tl = Lower(Letters(typed).core), al = Lower(Letters(there).core);
	for (const auto& e : exceptions) {
		if (Signed(e)) continue; // со знаками - только слово целиком (Excepted)
		const std::wstring el = Lower(Letters(e).core);
		if (el.empty()) continue;
		for (const std::wstring* w : { &tl, &al }) {
			if (w->empty()) continue;
			if (el.size() >= w->size() && el.compare(0, w->size(), *w) == 0) return true;
			if (el.size() >= 3 && w->size() >= el.size() && w->compare(0, el.size(), el) == 0) return true;
		}
	}
	return false;
}

// Набранное - начало более длинного слова из исключений, в любой из двух форм: слово из "Переключать всегда" сразу, как
// набрано, не переключать - может, набирают то, из исключений ("Uhfylt" там, "Uhfyltt" здесь; Дмитрий, 26.10.2026).
inline bool ExceptionAhead(const std::wstring& typed, const std::wstring& there,
                           const std::vector<std::wstring>& exceptions) {
	const std::wstring tl = Lower(Letters(typed).core), al = Lower(Letters(there).core);
	for (const auto& e : exceptions) {
		const std::wstring el = Lower(Letters(e).core);
		for (const std::wstring* w : { &tl, &al })
			if (!w->empty() && el.size() > w->size() && el.compare(0, w->size(), *w) == 0) return true;
	}
	return false;
}

// Решение посреди слова. typed - набранное с начала слова, there - те же клавиши в другой раскладке; minLetters - с
// какой буквы можно переключать: kEarlyMin, после щелчка или стрелок в том же окне на одну больше (могли дописывать
// середину слова). knownTyped(буквы) - набранное - слово или начало слова своего языка (не знаем - да: не трогать);
// knownThere(буквы) - там начало слова.
inline EarlyVerdict DecideEarly(const std::wstring& typed, const std::wstring& there, size_t minLetters,
                                const std::vector<std::wstring>& exceptions, auto&& knownTyped, auto&& knownThere) {
	const Part t = Letters(typed), a = Letters(there);
	// Там только буквы: знак там - или знак и есть, или не слово; решит конец слова.
	if (a.core.empty() || a.inner || a.core.size() != there.size()) return { Early::Never, "a sign in the other layout" };
	for (wchar_t c : typed)
		if (iswdigit(c)) return { Early::Never, "digits" };
	const auto script = TwoCaps::ScriptOf(a.core[0]);
	if (script == TwoCaps::Script::Other) return { Early::Never, "not letters" };
	bool vowel = false;
	for (wchar_t c : a.core) {
		if (TwoCaps::ScriptOf(c) != script) return { Early::Never, "mixed letters" };
		vowel = vowel || IsVowel(c);
	}
	if (a.core.size() > kEarlyMax) return { Early::Never, "too long" };
	if (typed.size() >= 3 && typed[0] == typed[1] && typed[1] == typed[2]) return { Early::Never, "repeated letters" };
	if (AllUpper(t.core) && AllUpper(a.core) && t.core.size() == typed.size()) {
		if (a.core.size() < kCapsEarlyMin) return { Early::NotYet, "capitals: an abbreviation so far" };
		return DecideEarly(Lower(typed), Lower(there), minLetters, exceptions, knownTyped, knownThere);
	}
	if (AllUpper(t.core) || AllUpper(a.core)) return { Early::Never, "abbreviation" };
	// Заглавная внутри: LShift, KBps, iPhone - имена и сокращения, или ДВе ЗАглавные (GHbdtn); решит конец слова.
	for (size_t i = 1; i < t.core.size(); i++)
		if (TwoCaps::IsUpper(t.core[i])) return { Early::Never, "a capital inside" };
	if (a.core.size() < minLetters) return { Early::NotYet, "too short" };
	if (!vowel) return { Early::NotYet, "no vowels yet" }; // "ыек" - str..., но и "кгы" - не слово
	if (ExceptedEarly(typed, there, exceptions)) return { Early::NotYet, "exception" };
	// Буквой раньше и сейчас. Знак в начале, который там буква (",elm" - "будь"), - решит конец слова: посреди слова
	// кавычка и скобка - обычное начало ("\"cre" - не "Эску", "<htt" - не "Брее").
	for (size_t n : { typed.size() - 1, typed.size() }) {
		const Part tn = Letters(typed.substr(0, n));
		if (!tn.core.empty() && knownTyped(tn.core)) return { Early::NotYet, "a word or its beginning as typed" };
		if (!knownThere(there.substr(0, n))) return { Early::NotYet, "not a beginning of a word in the other layout" };
	}
	return { Early::Switch, nullptr };
}

// ----- Короткие слова: по частоте и соседям -----
// Словари Windows считают словом любую букву (f, b, ф, ш) и многие сокращения из двух-трёх букв (ns, vs, pf, bp, tot,
// ult; ин, ща, иге), поэтому "а", "и", "в", "ты", "мы", "за", "из", "ещё", "где", набранные в английской раскладке, и
// "a", "I", "by", "of", "but", набранные в русской, правило выше не переключает. Здесь решают частота (ShortWords.h:
// частые слова из одной-трёх букв) и соседи:
//   - контекст - слово перед этим в том же куске текста (через пробел, в той же раскладке; щелчок, стрелки, Enter,
//     другое окно начинают новый кусок): Start - его нет (или набрано в другой раскладке), Same - это слово языка
//     раскладки или его переключили в эту раскладку, Unknown - набрано в этой раскладке, но не слово;
//   - одно короткое слово само не переключается никогда: одна буква - переменные b, c, x, "plan B"; две-три - ns, bp,
//     ye бывают и английскими (переменная, "nice shot", фамилия). Нужно подтверждение - соседи;
//   - когда слово переключается, до трёх коротких слов перед ним, набранных в той же раскладке и оставленных,
//     переводятся вместе с ним (Retro): "f vj;yj" - "а можно", "dj dhtvz" - "во время"; частое там слово - если там оно
//     не реже ("of" перед русским словом остаётся: of чаще, чем "ща"). Не переводится слово после числа ("50 шт" -
//     единица), после слова своего языка ("type C", "plan B") и после ручной смены раскладки ("TV" в русском тексте);
//   - две-три буквы, которые словарь пропускает (ShortWord: там частое слово, набранное - нет, контекст не Same),
//     переключаются, если перед ними такое же короткое слово, которое переводится вместе с ними: "ns ult" - "ты где",
//     "f ns" - "а ты"; одно - ждёт следующего слова;
//   - знак после короткого слова, какой в английском за буквой не ставят, а в русской раскладке это "?", ":", ";"
//     (SignEvidence): "f&" - "а?", "ns&" - "ты?" - переключает сразу, и одну букву.
// Слова - только через пробел: Tab переводит в другое поле или ячейку (CycleRevertList::TailWords).
// Частота набранного (Weight): русскому словарю в коротких словах можно верить - слово, которое он знает ("учу", "ща",
// "шт"), - настоящее, даже если его нет в списке; английскому нельзя (ShortWords::TrustDictionary).
// Проверено фразами целиком (tools/test_autoswitch.cmd --sentences, 06.10.2026): 700 фраз чатов, писем и кода (написаны
// для проверки) и 2 100 предложений романа (отрывок; в проект не входит). Набранное правильно короткими словами не
// тронуто ни разу; набранное целиком не в той раскладке выходит правильным: русские чаты и письма 99,6-100 %, английские
// 87 % (I'm, it's - со знаком внутри), роман 96 % (остальное - выдуманные слова и имена, которых нет в словаре).
enum class Context { Start, Same, Unknown };

// Насколько обычно набранное короткое слово lower (строчными; original - как набрано) в языке lang: 0 - не обычное.
inline int Weight(const std::wstring& lower, const std::wstring& original, const std::wstring& lang, auto&& spell) {
	if (lower.empty()) return 0;
	if (lower.size() == 1) return ShortWords::OneLetter(lower, lang) ? 3 : 0;
	int band = ShortWords::Band(lower, lang);
	if (band < 2 && ShortWords::TrustDictionary(lang) && !ShortWords::NotWord(lower, lang) &&
	    spell(original) == SpellCheck::Result::Word)
		band = 2;
	return band;
}

// Контекст по слову перед этим (как набрано; lang - язык его раскладки, spell - словарь этого языка).
inline Context ContextOf(const std::wstring& prev, const std::wstring& lang, auto&& spell) {
	const Part p = Letters(prev);
	if (p.core.empty()) {
		// Число перед словом ("5 шт", "512 kb"): дальше - единица или сокращение того же языка.
		for (wchar_t c : prev)
			if (iswdigit(c)) return Context::Same;
		return Context::Unknown; // знаки
	}
	const std::wstring l = Lower(p.core);
	if (l.size() == 1) return ShortWords::OneLetter(l, lang) ? Context::Same : Context::Unknown;
	if (ShortWords::Neutral(l, lang)) return Context::Unknown; // ok, lol латиницей пишут и среди русских слов
	// Две-три буквы, а словарь берёт любые сокращения (ns, ult): слово своего языка - только из списка частых.
	if (l.size() <= 3 && !ShortWords::TrustDictionary(lang))
		return ShortWords::Frequent(l, lang) ? Context::Same : Context::Unknown;
	return ShortWords::Frequent(l, lang) || spell(p.core) == SpellCheck::Result::Word ? Context::Same : Context::Unknown;
}

// Знак после короткого слова, какой в английском за буквой не ставят, а в русской раскладке это вопрос или двоеточие
// ("f&" - "а?", "x`&" - "чё?", "lf^" - "да:"), и точка "/" после двух-трёх букв там ("lf/" - "да."): сам по себе знак,
// что набрано не в той раскладке. Один, сразу после букв обоих прочтений ("&&" - уже C++).
inline bool SignEvidence(const std::wstring& typed, const Part& t, const Part& a) {
	const size_t from = (std::max)(t.end, a.end);
	if (a.core.empty() || from + 1 != typed.size()) return false;
	const wchar_t c = typed[from];
	return c == L'&' || c == L'^' || (c == L'/' && a.core.size() >= 2);
}

enum class Short {
	No,
	WithPartner, // переключить, если перед ним короткое слово, которое переводится вместе с ним (RetroCount)
	Now,         // переключить сразу: знак после него (SignEvidence)
};

// Короткое слово, которое словарь пропускает: две-три буквы там (сокращение: ns - "ты", tot - "еще"; или знак, который
// там буква: "yb[" - "них", "b[" - "их") или одна буква со знаком ("f&" - "а?").
inline Short ShortWord(const std::wstring& typed, const std::wstring& there, const std::wstring& typedLang,
                       const std::wstring& otherLang, Context context, auto&& spellTyped) {
	const Part t = Letters(typed), a = Letters(there);
	if (t.inner || a.inner || a.core.empty() || a.core.size() > 3 || t.core.size() > a.core.size()) return Short::No;
	for (wchar_t c : typed)
		if (iswdigit(c)) return Short::No;
	if (t.core.size() > 1 && AllUpper(t.core)) return Short::No;
	const std::wstring tl = Lower(t.core), al = Lower(a.core);
	const bool sign = SignEvidence(typed, t, a);
	// Одна буква - только строчная, со знаком и не после слова своего языка ("const B& b" - C++).
	if (al.size() == 1)
		return sign && context != Context::Same && tl.size() == 1 && !TwoCaps::IsUpper(t.core[0]) &&
				ShortWords::OneLetter(al, otherLang) && !ShortWords::OneLetter(tl, typedLang)
			? Short::Now
			: Short::No;
	// Буква там - из клавиши после набранных букв, и это не знак после слова ("to`" - "ещё", но "it." - не "шею"): набрано
	// не то слово, какое видно ("to").
	const bool letterAfter = a.end > t.end && typed.find_first_not_of(L".,;:!?'\")", t.end) < a.end;
	if (!ShortWords::Frequent(al, otherLang) || (!letterAfter && Weight(tl, t.core, typedLang, spellTyped) != 0))
		return Short::No;
	if (sign) return Short::Now;
	return context == Context::Same ? Short::No : Short::WithPartner;
}

// Частое короткое слово своего языка ("шт", "руб", "ул", "gb"): правило конца слова его не трогает, даже если словарь
// его не знает, а там - слово ("5 шт" - не "5 in").
inline bool FrequentAsTyped(const std::wstring& typed, const std::wstring& lang) {
	const Part t = Letters(typed);
	return !t.inner && t.core.size() >= 2 && t.core.size() <= 3 && ShortWords::Frequent(Lower(t.core), lang);
}

// Слово перед переключённым, набранное в той же раскладке и оставленное: перевести вместе с ним? Одна буква - если там
// однобуквенное слово, а набранное - нет ("f" - "а"); две-три - если там частое слово или слово словаря, а набранное не
// частое; знак, который там буква (";t" - "же"), - если там частое слово.
inline bool Retro(const std::wstring& typed, const std::wstring& there, const std::wstring& typedLang,
                  const std::wstring& otherLang, auto&& spellTyped) {
	const Part t = Letters(typed), a = Letters(there);
	// Буква в букву: буква, ставшая там знаком ("руб" - "he,"), - не то слово.
	if (t.inner || a.inner || a.core.empty() || a.core.size() > 3 || t.core.size() > a.core.size()) return false;
	for (wchar_t c : typed)
		if (iswdigit(c)) return false;
	// ПРОПИСНЫЕ - аббревиатура, не переводится (KDE перед английским словом переводить пробовали в 1.5.16-test3: от МП, ПВЗ,
	// ТЗ, МГУ перед Ozon, Claude его не отличить; ревью 25.10.2026). Такое слово программа выучит, если его исправлять.
	if (t.core.size() > 1 && AllUpper(t.core)) return false;
	const std::wstring tl = Lower(t.core), al = Lower(a.core);
	if (al.size() == 1 && tl.size() == 1)
		return ShortWords::OneLetter(al, otherLang) && !ShortWords::OneLetter(tl, typedLang);
	// Слово, которое знает словарь, которому можно верить ("ща", "шт", "рук"), - своё.
	if (tl.size() >= 2 && ShortWords::TrustDictionary(typedLang) && !ShortWords::NotWord(tl, typedLang) &&
	    spellTyped(t.core) == SpellCheck::Result::Word)
		return false;
	const int typedWeight = tl.size() == 1 ? (ShortWords::OneLetter(tl, typedLang) ? 3 : 0) : ShortWords::Band(tl, typedLang);
	const int thereWeight = al.size() == 1 ? (ShortWords::OneLetter(al, otherLang) ? 3 : 0) : ShortWords::Band(al, otherLang);
	return thereWeight > 0 && thereWeight >= typedWeight;
}

// Перед словом, которое переключается, - короткие слова, переводятся вместе с ним (Retro); words - слова перед ним, с
// ближнего: что о каждом известно. Сколько перевести (подряд, с ближнего).
struct RetroWord {
	std::wstring typed, there; // как набрано и те же клавиши там
	bool sameLayout = true;    // набрано в той же раскладке, что и переключаемое слово
	bool fixedAfter = false;   // ... а это - последнее известное, и перед ним исправленное слово
};
inline size_t RetroCount(const std::vector<RetroWord>& words, const std::wstring& typedLang, const std::wstring& otherLang,
                         const std::vector<std::wstring>& exceptions, auto&& spellTyped) {
	auto hasDigit = [](const std::wstring& s) {
		for (wchar_t c : s)
			if (iswdigit(c)) return true;
		return false;
	};
	auto candidate = [&](const RetroWord& w) {
		return w.sameLayout && !Letters(w.typed).core.empty() && !Excepted(w.typed, w.there, exceptions) &&
			Retro(w.typed, w.there, typedLang, otherLang, spellTyped);
	};
	// Одна строчная буква, которая в своём языке словом не бывает, а там - частое слово из одной буквы ("d" - "в", "f" -
	// "а", "b" - "и"): перед переключаемым словом это оно, и после слова своего языка или исправленного ("GitHub d
	// if,kjyf[" - "GitHub в шаблонах": оставалось "d шаблонах", Дмитрий 07.10). Заглавная - обозначение ("plan B",
	// "type C", "vitamin D"), она остаётся - кроме начала предложения: перед ней . ! ? … ("North west. B gjnjve" - "North
	// west. И потому", Дмитрий 07.10).
	auto endsSentence = [](const std::wstring& s) {
		const size_t last = s.find_last_not_of(L" \t");
		return last != std::wstring::npos && wcschr(L".!?…", s[last]) != nullptr;
	};
	// Английское I - всегда заглавное, это не обозначение: и после слова своего языка ("так Ш цфы" - "так I was":
	// оставалось "так Ш was"; Дмитрий, 19.10.2026).
	auto letterThere = [&](const RetroWord& w, bool sentenceStart) {
		const Part t = Letters(w.typed), a = Letters(w.there);
		const bool pronounI = a.core == L"I" && ShortWords::Is(otherLang, L"en");
		return t.core.size() == 1 && a.core.size() == 1 && (sentenceStart || pronounI || !TwoCaps::IsUpper(t.core[0])) &&
			!ShortWords::OneLetter(Lower(t.core), typedLang) && ShortWords::OneLetter(Lower(a.core), otherLang);
	};
	size_t count = 0;
	for (size_t i = 0; i < words.size() && i < 5; i++) {
		const RetroWord& w = words[i];
		if (!w.sameLayout) break;
		const Part p = Letters(w.typed);
		if (p.core.empty()) {
			// Без букв - число, тире: там то же самое ("d 10 vbyen" - "в 10 минут") - пропустить; знаки, которые там
			// другие (":)" - "Ж)", "15^00" - "15:00"), - нет.
			if (w.typed != w.there) break;
			continue;
		}
		if (hasDigit(w.typed) || !candidate(w)) break;
		// Слово перед ним: число - это единица ("50 шт", "64 kb"); набрано в другой раскладке - его набрали так нарочно
		// ("TV"); перед одной буквой - слово своего языка, само не переводимое: и буква того языка ("type C", "plan B").
		if (i + 1 < words.size()) {
			const RetroWord& before = words[i + 1];
			if (!before.sameLayout) break;
			const Part b = Letters(before.typed);
			if (b.core.empty() && hasDigit(before.typed)) break;
			if (p.core.size() == 1 && !b.core.empty() && !candidate(before) && !letterThere(w, endsSentence(before.typed)) &&
			    ContextOf(before.typed, typedLang, spellTyped) == Context::Same)
				break;
		}
		else if (w.fixedAfter && p.core.size() == 1 && !letterThere(w, false))
			break;
		count = i + 1;
	}
	return count;
}

}
