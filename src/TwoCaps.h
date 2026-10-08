// "ДВе ЗАглавные" (two_caps): слово, набранное с двумя заглавными в начале, исправляется после пробела - "ДВух" ->
// "Двух". Правило узкое, чтобы не трогать то, что так и пишется:
//   - две заглавные, дальше строчные, и всё слово - буквы одного алфавита: eM, iPhone (не две заглавные в начале),
//     2FA, MP3s (цифры) не трогаются;
//   - кириллица - от трёх букв ("ЧТо", "ДЛя", "НЕт" - самые частые такие опечатки; Maz на форуме, 08.10.2026: "ни разу
//     не исправились ДВе... Таких сочетаний много"). Единицы (МГц, МВт, МПа...) - во встроенных исключениях. "СШа" (США с
//     Shift, отпущенным на букву раньше) станет "Сша" - так же неверно, как было; "Исправить последнее слово" вернёт;
//   - латиница из трёх букв - только частое английское слово ("THe", "WAs", "TRy"; Дмитрий: "кучу английских
//     трехбуквенных слов мы не обрабатываем"): PCs, IDs, GHz, CDs, VMs так и пишутся. Частые слова - список движка
//     (KnownEnglish); в нём есть и сокращения с "s" (pcs, ips, pos, ops, ais...), поэтому слово из двух заглавных и
//     "s" исправляется, только если это слово, а не сокращение во множественном числе: was, has, his, its, yes...
//     (WordWithS; словарь Windows тоже считает "IPs", "POs" словами);
//   - исключения: встроенные (VMware, OAuth...) и свои (two_caps_exceptions; "Исправить последнее слово" сразу
//     после исправления возвращает слово и добавляет его туда). Исключение действует и на слова, которые с него
//     начинаются: "ИПшник" - и "ИПшника", "ИПшники".
// Знаки по краям ("«ДВух»,") не мешают: исправляется только буквенная часть.
// Только правило, без клавиатуры (tools/test_twocaps.cmd); исправляет движок (WorkerImplement::FixTwoCaps).
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace TwoCaps {

// Слова, которые сами пишутся двумя заглавными (и с них начинаются другие).
inline const std::vector<std::wstring>& BuiltIn() {
	static const std::vector<std::wstring> words = {
		L"VMware", L"OAuth", L"IPsec", L"DBeaver", L"LTspice", L"KBps", L"MBps", L"GBps", L"TBps", L"ВКонтакте",
		// Единицы: мега-, гига-, тера- (кило- - строчная "к": кВт, кГц).
		L"МГц", L"ГГц", L"ТГц", L"МВт", L"ГВт", L"ТВт", L"МПа", L"ГПа", L"ТПа", L"МДж", L"ГДж", L"ТДж", L"МОм", L"ГОм",
		// Английские сокращения из трёх букв с "s" и единицы (на случай, если они есть в списке частых слов).
		L"PCs", L"IDs", L"OSs", L"PRs", L"CDs", L"TVs", L"VMs", L"DJs", L"MPs", L"GBs", L"MBs", L"KBs", L"TBs", L"GHz",
		L"MHz", L"KHz", L"THz",
	};
	return words;
}

// Частое английское слово (строчными) - для латинских слов из трёх букв. Ставит движок (WorkerImplement.cpp: список
// WORDS_EN_COMMON, WordStart.h); не поставлена (проверка правила без списков) - такие слова не исправляются.
inline bool (*KnownEnglish)(const std::wstring& lower) = nullptr;

// Частые английские слова из трёх букв на "s" - слова, а не сокращения во множественном числе (IPs, POs, OPs, AIs).
inline bool WordWithS(const std::wstring& lower) {
	for (const wchar_t* w : { L"was", L"has", L"his", L"its", L"yes", L"gas", L"bus", L"ifs", L"ins", L"ohs", L"ups" })
		if (lower == w) return true;
	return false;
}

enum class Script { Other, Latin, Cyrillic, Greek };

inline Script ScriptOf(wchar_t c) {
	if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7))
		return Script::Latin;
	if (c >= 0x400 && c <= 0x52F) return Script::Cyrillic;
	if (c >= 0x370 && c <= 0x3FF) return Script::Greek;
	return Script::Other;
}

inline bool IsUpper(wchar_t c) { return IsCharUpperW(c) != FALSE; }
inline bool IsLower(wchar_t c) { return IsCharLowerW(c) != FALSE; }
inline bool IsLetter(wchar_t c) { return IsCharAlphaW(c) != FALSE; }
inline wchar_t ToLower(wchar_t c) { return (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)c); }

// Слово во встроенных или своих исключениях - или начинается с исключения от четырёх букв ("ИПшник" - "ИПшника").
inline bool Excepted(const std::wstring& word, const std::vector<std::wstring>& exceptions) {
	auto excepted = [&word](const std::wstring& e) {
		return !e.empty() && (word == e || (e.size() >= 4 && word.compare(0, e.size(), e) == 0));
	};
	for (const auto& e : BuiltIn())
		if (excepted(e)) return true;
	for (const auto& e : exceptions)
		if (excepted(e)) return true;
	return false;
}

// Слово (только буквы) под правило и не в исключениях.
inline bool Matches(const std::wstring& word, const std::vector<std::wstring>& exceptions) {
	const Script script = word.empty() ? Script::Other : ScriptOf(word[0]);
	if (script == Script::Other) return false;
	if (word.size() < 3) return false;
	for (size_t i = 0; i < word.size(); i++) {
		const wchar_t c = word[i];
		if (ScriptOf(c) != script || !IsLetter(c)) return false;
		if (i < 2 ? !IsUpper(c) : !IsLower(c)) return false;
	}
	if (word.size() == 3 && script != Script::Cyrillic) {
		if (script != Script::Latin || !KnownEnglish) return false;
		std::wstring lower = word;
		for (auto& c : lower) c = ToLower(c);
		if (!KnownEnglish(lower)) return false;
		if (lower[2] == L's' && !WordWithS(lower)) return false;
	}
	return !Excepted(word, exceptions);
}

struct Fix {
	size_t from = 0;     // с этого символа набранного текста он меняется (вторая буква слова)
	std::wstring tail;   // чем заменить text.substr(from)
	std::wstring word;   // само слово, как набрано (для исключений)
	bool upper = false;  // меняемая буква станет заглавной (i - I, LoneI); иначе - строчной (ДВе ЗАглавные)
};

// Английское местоимение i отдельным словом - I, и i'm, i've, i'll, i'd - I'm, I've... (fix_lone_i; Дмитрий 07.10). text -
// набранное после прошлого пробела; знаки по краям ("(i", "i,") не мешают, цифры - мешают. "i" в исключениях - не
// исправлять (туда его кладёт третья отмена). Что раскладка английская и это не редактор кода - решает движок.
inline Fix LoneI(const std::wstring& text, const std::vector<std::wstring>& exceptions) {
	size_t begin = 0, end = text.size();
	while (begin < end && !IsLetter(text[begin])) begin++;
	while (end > begin && !IsLetter(text[end - 1])) end--;
	if (end <= begin || text[begin] != L'i') return {};
	// По краям - только знаки, какие бывают у слова в тексте: "(i", "i," - да; "i++", "i=0", "[i" - код.
	for (size_t i = 0; i < begin; i++)
		if (!wcschr(L"(\"'«“", text[i])) return {};
	for (size_t i = end; i < text.size(); i++)
		if (!wcschr(L".,!?;:)\"'»”…", text[i])) return {};
	const std::wstring word = text.substr(begin, end - begin);
	bool form = false;
	for (const wchar_t* f : { L"i", L"i'm", L"i've", L"i'll", L"i'd" })
		form = form || word == f;
	if (!form) return {};
	for (const auto& e : exceptions)
		if (e == L"i" || e == L"I") return {};
	Fix fix;
	fix.from = begin;
	fix.tail = text.substr(begin);
	fix.tail[0] = L'I';
	fix.word = L"i";
	fix.upper = true;
	return fix;
}

// text - всё, что набрано после прошлого пробела. Нечего исправлять - from = 0 и tail пустой.
inline Fix Analyze(const std::wstring& text, const std::vector<std::wstring>& exceptions) {
	size_t begin = 0, end = text.size();
	while (begin < end && !IsLetter(text[begin])) begin++;
	while (end > begin && !IsLetter(text[end - 1])) end--;
	if (end - begin < 3) return {}; // от трёх букв; сколько нужно для алфавита - Matches
	for (size_t i = begin; i < end; i++)
		if (!IsLetter(text[i])) return {}; // знак внутри слова ("ДВ-ух") - не наше
	for (size_t i = 0; i < begin; i++)
		if (IsLetter(text[i]) || iswdigit(text[i])) return {};
	for (size_t i = end; i < text.size(); i++)
		if (IsLetter(text[i]) || iswdigit(text[i])) return {};
	const std::wstring word = text.substr(begin, end - begin);
	if (!Matches(word, exceptions)) return {};
	Fix fix;
	fix.from = begin + 1;
	fix.tail = text.substr(fix.from);
	fix.tail[0] = ToLower(fix.tail[0]);
	fix.word = word;
	return fix;
}

// Весь текст: каждое слово между пробелами - по правилу (перевод раскладки: "LDe[" -> "ДВух" -> "Двух").
// Длина та же: меняется только регистр вторых букв.
inline std::wstring FixText(const std::wstring& text, const std::vector<std::wstring>& exceptions) {
	std::wstring out = text;
	size_t at = 0;
	while (at < out.size()) {
		while (at < out.size() && iswspace(out[at])) at++;
		size_t end = at;
		while (end < out.size() && !iswspace(out[end])) end++;
		if (end > at) {
			const Fix fix = Analyze(out.substr(at, end - at), exceptions);
			if (!fix.tail.empty()) out.replace(at + fix.from, fix.tail.size(), fix.tail);
		}
		at = end;
	}
	return out;
}

}
