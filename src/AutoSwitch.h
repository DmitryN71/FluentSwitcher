// Автопереключение раскладки (autoswitch): слово, набранное не в той раскладке, исправляется само в конце слова -
// на пробеле, Enter или Tab, - а когда можно, то и раньше, посреди слова (autoswitch_early, DecideEarly ниже). Правило
// и исключения - как в LangBar++ (Krot66, github.com/Krot66/LangBarXX, LGPL-3.0), но по словарям Windows
// (WinDictionary.h) и её предсказанию текста (WordStart.h). В конце слова:
//   - набранное - не слово своего языка, а те же клавиши в другой включённой раскладке - слово её языка;
//   - не трогаются: одна буква; слова с цифрами; аббревиатуры (все буквы заглавные - набранные или в другой
//     раскладке); слово без гласных в другой раскладке (сокращение: "ru" - не "кг"); три одинаковых знака в начале; в
//     другой раскладке не одно слово (знак внутри - адрес, почта, путь); свои исключения (в любой из двух форм: cv или
//     см); после щелчка или стрелок в том же окне - слова короче четырёх букв (могли дописывать середину слова).
//   - опечатка - не другая раскладка: "helo" (в русской раскладке "руды") - словарь знает "hello", на одну букву
//     иначе (слова латиницей от четырёх букв); буквы, ставшие там знаками, какими слова не начинаются и не кончаются
//     ("бувы" - ",eds", "дувх" - "led[");
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
// раскладке; minLetters - 2, после щелчка или стрелок в том же окне 4.
inline const char* Skip(const std::wstring& typed, const std::wstring& there, size_t minLetters,
                        const std::vector<std::wstring>& exceptions) {
	const Part t = Letters(typed), a = Letters(there);
	if (a.core.empty() || a.inner) return "not one word in the other layout";
	if (a.core.size() < minLetters) return "too short";
	if (a.core.size() > 30) return "too long";
	for (wchar_t c : typed)
		if (iswdigit(c)) return "digits";
	const auto script = TwoCaps::ScriptOf(a.core[0]);
	if (script == TwoCaps::Script::Other) return "not letters";
	bool vowel = false;
	for (wchar_t c : a.core) {
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

// Переключать ли (после Skip). spellTyped / spellThere - словарь языка набранного / другой раскладки
// (SpellCheck::Result по слову). Набранное должно быть точно не словом: со знаком внутри ("j,]`v" - "объём") или из
// одной буквы среди знаков (",s" - "бы") - не слово; иначе спрашиваем словарь (знаки по краям - знаки: "it." - слово
// "it"), нет словаря - не переключаем. Там - слово.
// suggestTyped - подсказки словаря языка набранного (только когда всё остальное за переключение).
// DecideWhy - почему не переключать (для журнала: что пропущено и почему); nullptr - переключать.
inline const char* DecideWhy(const std::wstring& typed, const std::wstring& there, auto&& spellTyped,
                             auto&& spellThere, auto&& suggestTyped) {
	const Part t = Letters(typed), a = Letters(there);
	if (!t.inner && !(t.core.size() <= 1 && a.core.size() > t.core.size())) {
		const auto asTyped = spellTyped(t.core);
		if (asTyped == SpellCheck::Result::Unknown) return "no dictionary of the typed language";
		if (asTyped == SpellCheck::Result::Word) return "a word as typed";
	}
	const auto inOther = spellThere(a.core);
	if (inOther == SpellCheck::Result::Unknown) return "no dictionary of the other layout";
	if (inOther == SpellCheck::Result::NotWord) return "not a word in the other layout";
	if (!t.inner && LooksLikeTypo(t.core, suggestTyped(t.core))) return "looks like a typo";
	return nullptr;
}
inline bool Decide(const std::wstring& typed, const std::wstring& there, auto&& spellTyped, auto&& spellThere,
                   auto&& suggestTyped) {
	return DecideWhy(typed, there, spellTyped, spellThere, suggestTyped) == nullptr;
}

// ----- Посреди слова (autoswitch_early) -----
// Правило LangBar++: после каждой буквы набранное - не слово и не начало слова своего языка, а те же клавиши в другой
// раскладке - начало слова её языка. Начала слов знает предсказание текста Windows (WordStart.h); набранное
// проверяется ещё и словарём (слово целиком, подсказки) - редкое начало слова предсказание может не знать.
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
	// Буквой раньше и сейчас.
	for (size_t n : { typed.size() - 1, typed.size() }) {
		const Part tn = Letters(typed.substr(0, n));
		if (!tn.core.empty() && knownTyped(tn.core)) return { Early::NotYet, "a word or its beginning as typed" };
		if (!knownThere(there.substr(0, n))) return { Early::NotYet, "not a beginning of a word in the other layout" };
	}
	return { Early::Switch, nullptr };
}

}
