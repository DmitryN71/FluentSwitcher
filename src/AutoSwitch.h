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
//   - слово после ручной смены раскладки, после Backspace, в поле пароля, в консоли - не трогает движок.
//   - слова из списка "Переключать всегда" (autoswitch_force, в нужном виде: the, a) переключаются и без словаря, и
//     одной буквой: "еру" (для словаря - русское слово) станет the, "ф" - a.
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

// Слово через дефис в другой раскладке, и дефисы - те же клавиши ("Dj-gthds[" - "Во-первых,"): одно слово.
inline bool SameHyphens(const std::wstring& typed, const std::wstring& there, const Part& a) {
	if (!OnlyHyphens(a.core) || !LongParts(a.core)) return false;
	for (size_t i = a.begin; i < a.end && i < typed.size(); i++)
		if (there[i] == L'-' && typed[i] != L'-') return false;
	return true;
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

// Гласная (латиница - и буквы с диакритикой, кириллица - и украинские, белорусские). Другие алфавиты - не знаем: да.
inline bool IsVowel(wchar_t c) {
	const wchar_t l = TwoCaps::ToLower(c);
	switch (TwoCaps::ScriptOf(c)) {
	case TwoCaps::Script::Latin: return wcschr(L"aeiouy", l) != nullptr || l >= 0xC0;
	case TwoCaps::Script::Cyrillic: return wcschr(L"аеёиоуыэюяіїєў", l) != nullptr;
	default: return true;
	}
}

// Слово из исключений - в любой из двух форм (cv или см).
inline bool Excepted(const std::wstring& typed, const std::wstring& there, const std::vector<std::wstring>& exceptions) {
	const auto tl = Lower(Letters(typed).core), al = Lower(Letters(there).core);
	for (const auto& e : exceptions) {
		const auto el = Lower(Letters(e).core);
		if (!el.empty() && (el == tl || el == al)) return true;
	}
	return false;
}

// Слово из списка "Переключать всегда" (в том виде, какой нужен: the, a): там - оно, а набрано не оно (набранное
// правильно не трогаем).
inline bool Forced(const std::wstring& typed, const std::wstring& there, const std::vector<std::wstring>& forced) {
	const Part t = Letters(typed), a = Letters(there);
	if (a.core.empty() || a.inner) return false;
	const auto tl = Lower(t.core), al = Lower(a.core);
	for (const auto& f : forced) {
		const auto fl = Lower(Letters(f).core);
		if (!fl.empty() && fl == al && fl != tl) return true;
	}
	return false;
}

// Почему слово не трогаем; nullptr - можно спрашивать словари. typed - как набрано, there - те же клавиши в другой
// раскладке; minLetters - 2, после щелчка или стрелок в том же окне 4 (перед словом в поле не буква - 2).
inline const char* Skip(const std::wstring& typed, const std::wstring& there, size_t minLetters,
                        const std::vector<std::wstring>& exceptions) {
	const Part t = Letters(typed), a = Letters(there);
	if (a.core.empty() || (a.inner && !SameHyphens(typed, there, a))) return "not one word in the other layout";
	if (a.core.size() < minLetters) return "too short";
	if (a.core.size() > 30) return "too long";
	for (wchar_t c : typed)
		if (iswdigit(c)) return "digits";
	const auto script = TwoCaps::ScriptOf(a.core[0]);
	if (script == TwoCaps::Script::Other) return "not letters";
	bool vowel = false;
	for (wchar_t c : a.core) {
		if (c == L'-') continue; // дефис слова через дефис (SameHyphens)
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
		if (i < a.begin && !wcschr(L"'\"(", there[i])) return "a letter turns into a sign at the start";
		if (i >= a.end && !wcschr(L".,;:!?'\")", there[i])) return "a letter turns into a sign at the end";
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
inline bool LooksLikeTypo(const std::wstring& word, const std::vector<std::wstring>& suggestions) {
	if (word.size() < 4 || TwoCaps::ScriptOf(word[0]) != TwoCaps::Script::Latin) return false;
	const std::wstring w = Lower(word);
	for (const auto& s : suggestions) {
		const std::wstring l = Lower(s);
		if (!l.empty() && l.front() == w.front() && l.back() == w.back() && OneEditApart(w, l)) return true;
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
	if (inOther == SpellCheck::Result::NotWord)
		return TypoThere(t, a, innerSign, spellTyped, suggestTyped, suggestThere) ? nullptr : "not a word in the other layout";
	if (!innerSign && !signFirst && LooksLikeTypo(t.core, suggestTyped(t.core))) return "looks like a typo";
	return nullptr;
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
	// "type C", "vitamin D"), она остаётся.
	auto letterThere = [&](const RetroWord& w) {
		const Part t = Letters(w.typed), a = Letters(w.there);
		return t.core.size() == 1 && a.core.size() == 1 && !TwoCaps::IsUpper(t.core[0]) &&
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
			if (p.core.size() == 1 && !b.core.empty() && !candidate(before) && !letterThere(w) &&
			    ContextOf(before.typed, typedLang, spellTyped) == Context::Same)
				break;
		}
		else if (w.fixedAfter && p.core.size() == 1 && !letterThere(w))
			break;
		count = i + 1;
	}
	return count;
}

}
