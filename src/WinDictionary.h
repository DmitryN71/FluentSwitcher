// Словари Windows (Windows Spell Checking API, Windows 8 и новее): есть ли такое слово в языке раскладки. Нужны
// ДВум ЗАглавным: "GJgsnrf" - не английское слово, а "попытка" - русское, значит, это чужая раскладка, и правило
// заглавных его не трогает (одно "Исправить последнее слово" сразу даёт "Попытка"). Словаря нет - Unknown,
// и решают без него. Только рабочий поток движка (проверки - там же, словари кэшируются).
#pragma once

#include <spellcheck.h>
#include <wrl/client.h>

#include <map>
#include <string>

namespace SpellCheck {

enum class Result { Unknown, Word, NotWord };

// word - как есть (регистр не важен), language - "ru-RU", "en-US".
inline Result Check(std::wstring word, const std::wstring& language) {
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
	if (!factory || word.empty()) return Result::Unknown;
	auto it = checkers.find(language);
	if (it == checkers.end()) {
		ComPtr<ISpellChecker> checker;
		BOOL supported = FALSE;
		if (SUCCEEDED(factory->IsSupported(language.c_str(), &supported)) && supported)
			factory->CreateSpellChecker(language.c_str(), &checker);
		it = checkers.emplace(language, checker).first;
	}
	if (!it->second) return Result::Unknown;
	for (auto& c : word) c = (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)c);
	ComPtr<IEnumSpellingError> errors;
	if (FAILED(it->second->Check(word.c_str(), &errors)) || !errors) return Result::Unknown;
	ComPtr<ISpellingError> error;
	return errors->Next(&error) == S_OK ? Result::NotWord : Result::Word;
}

}
