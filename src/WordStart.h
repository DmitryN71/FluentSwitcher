// Начала слов: начинаются ли с этих букв слова языка - по предсказанию текста Windows (TextPredictionGenerator,
// подсказки сенсорной клавиатуры; Windows 10 и новее, данные языка ставятся вместе с его клавиатурой). Словари проверки
// орфографии (WinDictionary.h) знают только целые слова, а автопереключению посреди слова (AutoSwitch::DecideEarly)
// надо знать начала. Предсказание знает частые слова и те, что набирали на этом компьютере; редкие - не всегда,
// поэтому набранное проверяется ещё и словарём (Typed).
// Ответ - из первых 50 подсказок: предсказание предлагает и исправления опечаток ("njk" - Kim), поэтому начало слова -
// только подсказка, которая начинается с этих букв. Больше 50 оно не даёт (проверено: 100 и 200 - то же самое).
// Нет предсказания для языка или оно не ответило за 300 мс - Unknown. Только рабочий поток движка (MTA); ответы
// кэшируются.
#pragma once

#include "WinDictionary.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Text.h>

#include <chrono>
#include <map>
#include <string>
#include <unordered_map>

namespace WordStart {

enum class Result { Unknown, Yes, No };

inline std::wstring Lower(std::wstring s) {
	for (auto& c : s) c = (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)c);
	return s;
}

namespace impl {
struct Language {
	winrt::Windows::Data::Text::TextPredictionGenerator generator{ nullptr };
	std::unordered_map<std::wstring, bool> known; // строчными: начало слова или нет
};

// Предсказание языка ("ru-RU", "en-US"), одно на язык; нет - пустое.
inline Language& For(const std::wstring& language) {
	using namespace winrt::Windows::Data::Text;
	static std::map<std::wstring, Language> languages;
	auto it = languages.find(language);
	if (it != languages.end()) return it->second;
	Language l;
	try {
		CoInitializeEx(nullptr, COINIT_MULTITHREADED); // уже инициализирован (словари) - не страшно
		TextPredictionGenerator generator(language);
		// Другой язык (этот не поддерживается - Windows подставит свой) или не установлено - нет.
		const std::wstring resolved = generator.ResolvedLanguage().c_str();
		if (!generator.LanguageAvailableButNotInstalled() && resolved.size() >= 2 && language.size() >= 2 &&
		    _wcsnicmp(resolved.c_str(), language.c_str(), 2) == 0) {
			l.generator = generator;
			// Первый ответ - сотни миллисекунд (загружаются данные языка): спросить заранее, не дожидаясь ответа, чтобы
			// первое слово не ждало его (Known ждёт 300 мс, потом - "не знаем").
			generator.GetCandidatesAsync(L"a", 1);
		}
	}
	catch (...) {
	}
	return languages.emplace(language, std::move(l)).first->second;
}
}

// Есть ли предсказание для языка (заодно - загрузить его заранее: первый ответ - десятки миллисекунд).
inline bool Available(const std::wstring& language) {
	return impl::For(language).generator != nullptr;
}

// Начинаются ли с prefix слова языка language (регистр не важен).
inline Result Known(const std::wstring& prefix, const std::wstring& language) {
	using namespace winrt::Windows::Data::Text;
	using winrt::Windows::Foundation::AsyncStatus;
	impl::Language& l = impl::For(language);
	if (!l.generator || prefix.empty()) return Result::Unknown;
	const std::wstring p = Lower(prefix);
	if (auto it = l.known.find(p); it != l.known.end()) return it->second ? Result::Yes : Result::No;
	try {
		auto request = l.generator.GetCandidatesAsync(p, 50, TextPredictionOptions::Predictions,
		                                              winrt::single_threaded_vector<winrt::hstring>());
		if (request.wait_for(std::chrono::milliseconds(300)) != AsyncStatus::Completed) {
			request.Cancel();
			return Result::Unknown;
		}
		bool yes = false;
		for (auto&& candidate : request.GetResults()) {
			const std::wstring c = Lower(candidate.c_str());
			if (c.size() >= p.size() && c.compare(0, p.size(), p) == 0) {
				yes = true;
				break;
			}
		}
		if (l.known.size() > 20000) l.known.clear();
		l.known.emplace(p, yes);
		return yes ? Result::Yes : Result::No;
	}
	catch (...) {
		return Result::Unknown;
	}
}

// Набранное посреди слова - слово или начало слова своего языка: слово целиком (словарь), начало по предсказанию или
// подсказка словаря, которая с него начинается. Нет предсказания для языка или оно не ответило - да: судить не по чему,
// не трогать.
inline bool Typed(const std::wstring& letters, const std::wstring& language) {
	if (!Available(language)) return true;
	if (SpellCheck::CheckAnyCase(letters, language) == SpellCheck::Result::Word) return true;
	if (Known(letters, language) != Result::No) return true;
	const std::wstring l = Lower(letters);
	for (const auto& s : SpellCheck::Suggest(letters, language, 10)) {
		const std::wstring w = Lower(s);
		if (w.size() > l.size() && w.compare(0, l.size(), l) == 0) return true;
	}
	return false;
}

// Те же клавиши в другой раскладке - начало слова её языка: по предсказанию; от четырёх букв - и слово из словаря.
inline bool There(const std::wstring& letters, const std::wstring& language) {
	if (!Available(language)) return false;
	if (Known(letters, language) == Result::Yes) return true;
	return letters.size() >= 4 && SpellCheck::CheckAnyCase(letters, language) == SpellCheck::Result::Word;
}

}
