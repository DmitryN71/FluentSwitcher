// Файл правил (rules.json, HANDOFF.md, раздел 12): добавления и удаления поверх встроенных списков Rules.h.
//   { "schema": 1, "version": "2026.11.05.1", "min_app": "1.6.0",
//     "lists": { "short_en": { "add": ["rdp"], "remove": [] }, "zones": { "add": ["kz"] } } }
// Схема новее известной или программа старше min_app - файл не берётся, остаются встроенные. Неизвестные ключи и списки
// пропускаются (старая программа читает новый файл). Слово, которое не годится для своего списка (буквы не того
// алфавита, прописные, длина), пропускается одно, а не весь файл. Удаление главнее добавления: слово в обоих - убрано.
// Списки пользователя ("Переключать всегда", "Не переключать") главнее файла - их движок проверяет отдельно.
// Без wxWidgets: нужен и движку, и окну настроек.
#pragma once

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "json.hpp"
#include "Rules.h"
#include "Update.h" // IsNewer

namespace RulesFile {

inline constexpr int kSchema = 1;
inline constexpr size_t kMaxWords = 2000; // на список, add и remove вместе
inline constexpr size_t kMaxLength = 32;

// Какие буквы можно в списке.
enum class Alphabet { Latin, LatinDigits, Cyrillic, CyrillicDots };

struct ListInfo {
	const char* key;
	Rules::Words Rules::Lists::* words;
	Alphabet alphabet;
	size_t maxLength;
};

// Все списки файла; имена - как в Rules::Lists.
inline const ListInfo kLists[] = {
	{ "short_en", &Rules::Lists::shortEn, Alphabet::Latin, kMaxLength },
	{ "short_ru", &Rules::Lists::shortRu, Alphabet::Cyrillic, kMaxLength },
	{ "keep_en", &Rules::Lists::keepEn, Alphabet::Latin, kMaxLength },
	{ "keep_ru", &Rules::Lists::keepRu, Alphabet::Cyrillic, kMaxLength },
	{ "dotted_ru", &Rules::Lists::dottedRu, Alphabet::CyrillicDots, kMaxLength },
	{ "number_en", &Rules::Lists::numberEn, Alphabet::Latin, 4 }, // длиннее четырёх движок не смотрит (NumberFix)
	{ "number_ru", &Rules::Lists::numberRu, Alphabet::Cyrillic, 4 },
	{ "zones", &Rules::Lists::zones, Alphabet::LatinDigits, kMaxLength },
	{ "neutral_en", &Rules::Lists::neutralEn, Alphabet::Latin, kMaxLength },
	{ "not_word_ru", &Rules::Lists::notWordRu, Alphabet::Cyrillic, kMaxLength },
};

inline std::wstring Wide(const std::string& s) {
	if (s.empty()) return {};
	const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
	if (n <= 0) return {};
	std::wstring w(n, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), w.data(), n);
	return w;
}

// Годится ли слово для списка: только строчные буквы своего алфавита; сокращение с точками - с точкой, без двух точек
// подряд и не с точки ("т.д.", "др.").
inline bool Fits(const std::wstring& w, Alphabet alphabet, size_t maxLength) {
	if (w.empty() || w.size() > maxLength) return false;
	auto latin = [](wchar_t c) { return c >= L'a' && c <= L'z'; };
	auto cyrillic = [](wchar_t c) { return (c >= L'а' && c <= L'я') || c == L'ё'; };
	bool letter = false;
	for (size_t i = 0; i < w.size(); i++) {
		const wchar_t c = w[i];
		switch (alphabet) {
		case Alphabet::Latin:
			if (!latin(c)) return false;
			break;
		case Alphabet::LatinDigits:
			if (!latin(c) && !(c >= L'0' && c <= L'9')) return false;
			break;
		case Alphabet::Cyrillic:
			if (!cyrillic(c)) return false;
			break;
		case Alphabet::CyrillicDots:
			if (c == L'.') {
				if (i == 0 || w[i - 1] == L'.') return false;
				break;
			}
			if (!cyrillic(c)) return false;
			break;
		}
		letter = letter || latin(c) || cyrillic(c);
	}
	return letter && (alphabet != Alphabet::CyrillicDots || w.find(L'.') != std::wstring::npos);
}

struct Result {
	bool ok = false;
	Rules::Lists lists;            // встроенные + файл (не ok - встроенные)
	std::string version;           // "version" файла
	size_t added = 0, removed = 0; // слов, которые что-то изменили
	std::vector<std::string> skipped; // "short_en: Rdp" - для журнала
	std::string error;             // не ok - почему
};

// Разобрать текст файла; appVersion - версия программы (FS_VERSION).
inline Result Parse(const std::string& text, const std::string& appVersion) {
	Result r;
	r.lists = Rules::Builtin();
	const auto json = nlohmann::json::parse(text, nullptr, false);
	if (!json.is_object()) {
		r.error = "not JSON";
		return r;
	}
	const auto schema = json.find("schema");
	if (schema == json.end() || !schema->is_number_integer()) {
		r.error = "no schema";
		return r;
	}
	if (schema->get<long long>() > kSchema) {
		r.error = "schema " + std::to_string(schema->get<long long>()) + " is newer than " + std::to_string(kSchema);
		return r;
	}
	const auto version = json.find("version");
	if (version == json.end() || !version->is_string() || version->get<std::string>().empty()) {
		r.error = "no version";
		return r;
	}
	r.version = version->get<std::string>();
	if (const auto minApp = json.find("min_app"); minApp != json.end()) {
		if (!minApp->is_string()) {
			r.error = "min_app is not a string";
			return r;
		}
		if (Update::IsNewer(minApp->get<std::string>(), appVersion)) {
			r.error = "needs FluentSwitcher " + minApp->get<std::string>();
			return r;
		}
	}
	const auto lists = json.find("lists");
	if (lists == json.end() || !lists->is_object()) {
		r.error = "no lists";
		return r;
	}
	// Слова одного списка: годные - в words, негодные - в skipped.
	auto read = [&](const nlohmann::json& list, const char* what, const ListInfo& info, size_t& count) {
		std::vector<std::wstring> words;
		const auto it = list.find(what);
		if (it == list.end()) return words;
		if (!it->is_array()) {
			r.skipped.push_back(std::string(info.key) + "." + what + ": not a list");
			return words;
		}
		for (const auto& e : *it) {
			const std::string s = e.is_string() ? e.get<std::string>() : e.dump();
			const std::wstring w = e.is_string() ? Wide(s) : std::wstring();
			if (count >= kMaxWords) {
				r.skipped.push_back(std::string(info.key) + ": more than " + std::to_string(kMaxWords) + " words");
				break;
			}
			count++;
			if (!Fits(w, info.alphabet, info.maxLength)) {
				r.skipped.push_back(std::string(info.key) + ": " + s);
				continue;
			}
			words.push_back(w);
		}
		return words;
	};
	for (const ListInfo& info : kLists) {
		const auto list = lists->find(info.key);
		if (list == lists->end()) continue;
		if (!list->is_object()) {
			r.skipped.push_back(std::string(info.key) + ": not an object");
			continue;
		}
		size_t count = 0;
		Rules::Words& target = r.lists.*info.words;
		const auto add = read(*list, "add", info, count), remove = read(*list, "remove", info, count);
		for (const auto& w : remove)
			if (target.erase(w)) r.removed++;
		for (const auto& w : add)
			if (std::ranges::find(remove, w) == remove.end() && target.insert(w).second) r.added++;
	}
	r.lists.version = Wide(r.version);
	r.ok = true;
	return r;
}

}
