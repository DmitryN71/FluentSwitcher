// Словари Windows (Windows Spell Checking API, Windows 8 и новее): есть ли такое слово в языке раскладки. Нужны
// ДВум ЗАглавным: "GJgsnrf" - не английское слово, а "попытка" - русское, значит, это чужая раскладка, и правило
// заглавных его не трогает (одно "Исправить последнее слово" сразу даёт "Попытка"). Словаря нет - Unknown,
// и решают без него. Только рабочий поток движка (проверки - там же, словари кэшируются).
#pragma once

#include <spellcheck.h>
#include <wrl/client.h>

#include <map>
#include <string>
#include <vector>

namespace SpellCheck {

enum class Result { Unknown, Word, NotWord };

// Словарь языка ("ru-RU", "en-US"), один на язык; нет - пусто.
inline ISpellChecker* Checker(const std::wstring& language) {
	using Microsoft::WRL::ComPtr;
	static ComPtr<ISpellCheckerFactory> factory;
	static std::map<std::wstring, ComPtr<ISpellChecker>> checkers;
	static bool started = false;
	if (!started) {
		started = true;
		CoInitializeEx(nullptr, COINIT_MULTITHREADED); // уже инициализирован - не страшно
		if (FAILED(CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
			factory.Reset();
	}
	if (!factory) return nullptr;
	auto it = checkers.find(language);
	if (it == checkers.end()) {
		ComPtr<ISpellChecker> checker;
		BOOL supported = FALSE;
		if (SUCCEEDED(factory->IsSupported(language.c_str(), &supported)) && supported)
			factory->CreateSpellChecker(language.c_str(), &checker);
		it = checkers.emplace(language, checker).first;
	}
	return it->second.Get();
}

// word - как есть (регистр не важен: проверяется строчными; keepCase - как набрано), language - "ru-RU", "en-US".
inline Result Check(std::wstring word, const std::wstring& language, bool keepCase = false) {
	using Microsoft::WRL::ComPtr;
	ISpellChecker* checker = Checker(language);
	if (!checker || word.empty()) return Result::Unknown;
	if (!keepCase)
		for (auto& c : word) c = (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)c);
	ComPtr<IEnumSpellingError> errors;
	if (FAILED(checker->Check(word.c_str(), &errors)) || !errors) return Result::Unknown;
	ComPtr<ISpellingError> error;
	return errors->Next(&error) == S_OK ? Result::NotWord : Result::Word;
}

// Что словарь предлагает вместо слова с ошибкой (первые max).
inline std::vector<std::wstring> Suggest(const std::wstring& word, const std::wstring& language, size_t max = 10) {
	using Microsoft::WRL::ComPtr;
	std::vector<std::wstring> out;
	ISpellChecker* checker = Checker(language);
	ComPtr<IEnumString> list;
	if (!checker || word.empty() || FAILED(checker->Suggest(word.c_str(), &list)) || !list) return out;
	LPOLESTR s = nullptr;
	while (out.size() < max && list->Next(1, &s, nullptr) == S_OK) {
		out.emplace_back(s);
		CoTaskMemFree(s);
	}
	return out;
}

// Слово как набрано или строчными: имена собственные ("Москва") словарь знает только с заглавной, а "Ghbdtn" надо
// проверить и как "ghbdtn" (автопереключение, AutoSwitch.h).
inline Result CheckAnyCase(const std::wstring& word, const std::wstring& language) {
	const Result asTyped = Check(word, language, true);
	if (asTyped != Result::NotWord) return asTyped;
	bool lower = true; // набрано строчными - второй раз спрашивать то же незачем
	for (wchar_t c : word) lower = lower && !IsCharUpperW(c);
	return lower ? asTyped : Check(word, language);
}

// Слово набрано не в той раскладке: `typed` - не слово языка `language`, а `other` (те же клавиши в другой раскладке) -
// слово языка `otherLanguage`. Знаки по краям ("«Ыещз»", "Ghbdtn?") не в счёт. Нет словаря - false.
inline bool WrongLayout(const std::wstring& typed, const std::wstring& language, const std::wstring& other,
                        const std::wstring& otherLanguage) {
	auto letters = [](const std::wstring& s) {
		size_t begin = 0, end = s.size();
		while (begin < end && !IsCharAlphaW(s[begin])) begin++;
		while (end > begin && !IsCharAlphaW(s[end - 1])) end--;
		return s.substr(begin, end - begin);
	};
	const std::wstring a = letters(typed), b = letters(other);
	return !a.empty() && !b.empty() && Check(a, language) == Result::NotWord && Check(b, otherLanguage) == Result::Word;
}

}
