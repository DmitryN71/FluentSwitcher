// Начала слов: начинаются ли с этих букв слова языка. Словари проверки орфографии (WinDictionary.h) знают только целые
// слова, а автопереключению посреди слова (AutoSwitch::DecideEarly) надо знать начала. Списки частых слов встроены в
// программу (ресурсы WORDS_RU, WORDS_EN, WORDS_UK - src/data/words_ru.txt, words_en.txt, words_uk.txt): частотные
// списки FrequencyWords (Hermit Dave, по субтитрам OpenSubtitles 2018, CC BY-SA 4.0), слова от трёх букв, встретившиеся
// не меньше трёх раз и известные словарю Windows (украинские - без словаря, src/data/README.md;
// tools/corpus/make_wordlist.cmd); строчными, "ё" как "е". Своё набранное - по полному списку (чем больше слов, тем
// реже трогаем своё), а начало слова в другой раскладке - только по частым словам (WORDS_RU_COMMON, WORDS_EN_COMMON,
// WORDS_UK_COMMON - не меньше 50 раз, украинские - 5: их субтитров в 30 раз меньше): редкое слово там (Visby, сурьма)
// переключало бы опечатку в своём ("мшыи" - visb…). Поиск - двоичный, в памяти программы, за доли микросекунды и без
// обращений к Windows. (До 1.5.0-test9 здесь было предсказание текста Windows, TextPredictionGenerator: его обслуживает
// ctfmon - служба ввода, через которую идут набор, каретка и фокус всех программ, и долгая нагрузка его тормозила.)
// Набранное проверяется ещё и словарём (Typed): редкого слова в списке может не быть. Нет списка для языка - Unknown.
#pragma once

#include "WinDictionary.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace WordStart {

enum class Result { Unknown, Yes, No };

inline std::wstring Lower(std::wstring s) {
	for (auto& c : s) c = (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)c);
	return s;
}

namespace impl {
// Слова через \n, по порядку байтов UTF-8; starts - где каждое начинается.
struct List {
	std::string_view data;
	std::vector<uint32_t> starts;
	std::string file; // проверка: список из файла (у программы - ресурс)
};

// Проверка (tools\test_autoswitch.cpp): папка с words_*.txt - у неё ресурсов нет.
inline std::wstring dataDir;

// Как в списках: строчными, "ё" как "е", UTF-8.
inline std::string Key(const std::wstring& prefix) {
	std::wstring w = Lower(prefix);
	for (auto& c : w)
		if (c == L'ё') c = L'е';
	if (w.empty()) return {};
	std::string s(WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), (int)s.size(), nullptr, nullptr);
	return s;
}

// Список языка ("ru-RU", "en-US" - по первым двум буквам; common - только частые слова); нет - nullptr.
inline const List* For(const std::wstring& language, bool common = false) {
	static std::map<std::wstring, List> lists;
	if (language.size() < 2) return nullptr;
	const std::wstring code = Lower(language.substr(0, 2)) + (common ? L"_common" : L"");
	auto it = lists.find(code);
	if (it == lists.end()) {
		List& l = lists[code];
		std::wstring name = L"WORDS_" + code;
		for (auto& c : name) c = (wchar_t)towupper(c);
		if (HRSRC res = FindResourceW(nullptr, name.c_str(), MAKEINTRESOURCEW(10) /* RT_RCDATA */)) {
			if (HGLOBAL g = LoadResource(nullptr, res))
				l.data = std::string_view((const char*)LockResource(g), SizeofResource(nullptr, res));
		}
		else if (!dataDir.empty()) {
			std::ifstream in(dataDir + L"\\words_" + code + L".txt", std::ios::binary);
			l.file.assign(std::istreambuf_iterator<char>(in), {});
			l.data = l.file;
		}
		for (size_t i = 0; i < l.data.size();) {
			const size_t end = l.data.find('\n', i);
			if (end != i) l.starts.push_back((uint32_t)i);
			if (end == std::string_view::npos) break;
			i = end + 1;
		}
		it = lists.find(code);
	}
	return it->second.starts.empty() ? nullptr : &it->second;
}

inline std::string_view Word(const List& l, uint32_t start) {
	const size_t end = l.data.find('\n', start);
	std::string_view w = l.data.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
	if (!w.empty() && w.back() == '\r') w.remove_suffix(1); // список с переводами строк Windows
	return w;
}

// Номер буквы в таблицах сочетаний (Combos): 0 - край слова, русские и украинские буквы, латиница; не буква - -1.
inline int LetterIndex(wchar_t c) {
	c = (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)c);
	if (c == L'ё') c = L'е';
	if (c >= L'а' && c <= L'я') return 1 + (c - L'а'); // 1..32
	switch (c) {
	case L'і': return 33;
	case L'ї': return 34;
	case L'є': return 35;
	case L'ґ': return 36;
	}
	if (c >= L'a' && c <= L'z') return 37 + (c - L'a'); // 37..62
	return -1;
}

// Во скольких словах списка есть каждое сочетание двух и трёх букв (с краями слова: "^ф", "щк$").
struct Combos {
	static constexpr size_t kLetters = 64;
	std::vector<uint32_t> two = std::vector<uint32_t>(kLetters * kLetters);
	std::vector<uint32_t> three = std::vector<uint32_t>(kLetters * kLetters * kLetters);
};
}

// Сочетания букв языка - по полному списку его слов (AutoSwitch::Implausible): строятся при первом вопросе (на русском
// списке - доли секунды; WorkerImplement::WarmUpEarly - заранее). Нет списка - nullptr.
inline const impl::Combos* Combos(const std::wstring& language) {
	static std::map<std::wstring, std::unique_ptr<impl::Combos>> all;
	const impl::List* l = impl::For(language);
	if (!l) return nullptr;
	auto& combos = all[Lower(language.substr(0, 2))];
	if (!combos) {
		combos = std::make_unique<impl::Combos>();
		constexpr size_t n = impl::Combos::kLetters;
		std::wstring w;
		std::vector<int> index;
		std::vector<size_t> two, three;
		for (uint32_t start : l->starts) {
			const std::string_view u = impl::Word(*l, start);
			w.resize(MultiByteToWideChar(CP_UTF8, 0, u.data(), (int)u.size(), nullptr, 0));
			MultiByteToWideChar(CP_UTF8, 0, u.data(), (int)u.size(), w.data(), (int)w.size());
			index.assign(1, 0);
			for (wchar_t c : w) index.push_back(impl::LetterIndex(c));
			index.push_back(0);
			if (std::ranges::find(index, -1) != index.end()) continue;
			// Каждое сочетание - раз на слово.
			two.clear();
			three.clear();
			for (size_t i = 0; i + 1 < index.size(); i++) two.push_back(index[i] * n + index[i + 1]);
			for (size_t i = 0; i + 2 < index.size(); i++) three.push_back((index[i] * n + index[i + 1]) * n + index[i + 2]);
			for (auto* v : { &two, &three }) {
				std::ranges::sort(*v);
				v->erase(std::unique(v->begin(), v->end()), v->end());
			}
			for (size_t g : two) combos->two[g]++;
			for (size_t g : three) combos->three[g]++;
		}
	}
	return combos.get();
}

// Есть ли списки для языка (заодно - загрузить их заранее).
inline bool Available(const std::wstring& language) {
	return impl::For(language) != nullptr && impl::For(language, true) != nullptr;
}

// Начинаются ли с prefix слова языка language (регистр не важен); common - частые слова.
inline Result Known(const std::wstring& prefix, const std::wstring& language, bool common = false) {
	const impl::List* l = impl::For(language, common);
	if (!l || prefix.empty()) return Result::Unknown;
	const std::string key = impl::Key(prefix);
	const auto it = std::lower_bound(l->starts.begin(), l->starts.end(), key,
	                                 [l](uint32_t start, const std::string& k) { return impl::Word(*l, start) < k; });
	return it != l->starts.end() && impl::Word(*l, *it).starts_with(key) ? Result::Yes : Result::No;
}

// Слово целиком в списке языка (регистр не важен); common - в списке частых.
inline bool IsWord(const std::wstring& word, const std::wstring& language, bool common = false) {
	const impl::List* l = impl::For(language, common);
	if (!l || word.empty()) return false;
	const std::string key = impl::Key(word);
	const auto it = std::lower_bound(l->starts.begin(), l->starts.end(), key,
	                                 [l](uint32_t start, const std::string& k) { return impl::Word(*l, start) < k; });
	return it != l->starts.end() && impl::Word(*l, *it) == key;
}

// Набранное посреди слова - слово или начало слова своего языка: слово целиком (словарь), начало по списку или
// подсказка словаря, которая с него начинается. Нет списка для языка - да: судить не по чему, не трогать.
inline bool Typed(const std::wstring& letters, const std::wstring& language) {
	if (!Available(language)) return true;
	// Сначала свой список (доли микросекунды), потом словарь Windows (поход в COM): ответ тот же, "или".
	if (Known(letters, language) != Result::No) return true;
	if (SpellCheck::CheckAnyCase(letters, language) == SpellCheck::Result::Word) return true;
	const std::wstring l = Lower(letters);
	for (const auto& s : SpellCheck::Suggest(letters, language, 10)) {
		const std::wstring w = Lower(s);
		if (w.size() > l.size() && w.compare(0, l.size(), l) == 0) return true;
	}
	return false;
}

// То же для набранного ПРОПИСНЫМИ - как с заглавной: словарь Windows прописные принимает любые ("АИЩ" для него
// слово), и выученные в "Переключать всегда" FBO, FBS ("АИЩ", "АИЫ") ждали пробела (Дмитрий, 22.10.2026).
inline bool TypedEvenCaps(const std::wstring& letters, const std::wstring& language) {
	size_t upper = 0;
	for (wchar_t c : letters) {
		if (IsCharLowerW(c)) return Typed(letters, language);
		if (IsCharUpperW(c)) upper++;
	}
	if (upper < 2) return Typed(letters, language);
	std::wstring title = Lower(letters);
	title[0] = letters[0];
	return Typed(title, language);
}

// Те же клавиши в другой раскладке - начало частого слова её языка; от четырёх букв - и слово из словаря.
inline bool There(const std::wstring& letters, const std::wstring& language) {
	if (!Available(language)) return false;
	if (Known(letters, language, true) == Result::Yes) return true;
	return letters.size() >= 4 && SpellCheck::CheckAnyCase(letters, language) == SpellCheck::Result::Word;
}

}
