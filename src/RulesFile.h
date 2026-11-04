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
#include <atomic>
#include <fstream>
#include <iterator>
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

// ----- Откуда правила -----
// Свой файл рядом с программой (rules.json: положил пользователь, офлайн - gutasiho, форум 03.11.2026) главнее скачанного
// (rules-github.json, туда же); пока он лежит, с GitHub ничего не качается. Беты берут rules-beta.json: новое правило
// сначала получают они; его нет - rules.json.
inline const wchar_t kAddress[] = L"https://raw.githubusercontent.com/DmitryN71/FluentSwitcher/rules/rules.json";
inline const wchar_t kBetaAddress[] = L"https://raw.githubusercontent.com/DmitryN71/FluentSwitcher/rules/rules-beta.json";

inline std::wstring LocalPath(const std::wstring& folder) { return folder + L"\\rules.json"; }
inline std::wstring CachePath(const std::wstring& folder) { return folder + L"\\rules-github.json"; }

inline bool IsBeta(const std::string& appVersion) { return appVersion.find('-') != std::string::npos; }

inline std::vector<std::wstring> Addresses(const std::string& appVersion) {
	if (IsBeta(appVersion)) return { kBetaAddress, kAddress };
	return { kAddress };
}

enum class Source { Builtin, Local, Cache };

inline const char* SourceName(Source s) {
	return s == Source::Local ? "rules.json" : s == Source::Cache ? "rules-github.json" : "built-in";
}

// Что сейчас в работе (Load, Refresh).
inline std::atomic<Source>& Current() {
	static std::atomic<Source> s{ Source::Builtin };
	return s;
}

inline bool ReadText(const std::wstring& path, std::string& text) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

inline bool Exists(const std::wstring& path) {
	const DWORD a = GetFileAttributesW(path.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Когда скачанный файл в последний раз получен с GitHub (его время записи), мс от 1970; нет файла - 0.
inline long long CacheTime(const std::wstring& folder) {
	WIN32_FILE_ATTRIBUTE_DATA d{};
	if (!GetFileAttributesExW(CachePath(folder).c_str(), GetFileExInfoStandard, &d)) return 0;
	const ULONGLONG t = ((ULONGLONG)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
	return (long long)((t - 116444736000000000ULL) / 10000); // от 1601 года, по 100 нс
}

struct Loaded {
	Source source = Source::Builtin;
	std::string version;
	size_t added = 0, removed = 0;
	std::vector<std::string> skipped;
	std::vector<std::string> notes; // что не взято и почему - для журнала
};

// При запуске и после "Проверить сейчас": свой файл, иначе скачанный (downloaded - "Обновлять правила" включено), иначе
// встроенные. Не разобрался свой - как будто его нет. Выставляет списки (Rules::Set).
inline Loaded Load(const std::wstring& folder, bool downloaded, const std::string& appVersion) {
	Loaded l;
	auto take = [&](Source source, const std::wstring& path) {
		std::string text;
		if (!ReadText(path, text)) return false;
		Result r = Parse(text, appVersion);
		if (!r.ok) {
			l.notes.push_back(std::string(SourceName(source)) + ": " + r.error);
			return false;
		}
		Rules::Set(std::move(r.lists));
		l.source = source;
		l.version = r.version;
		l.added = r.added;
		l.removed = r.removed;
		l.skipped = std::move(r.skipped);
		return true;
	};
	if (!take(Source::Local, LocalPath(folder)) && !(downloaded && take(Source::Cache, CachePath(folder)))) {
		if (Current() != Source::Builtin) Rules::Set(Rules::Builtin());
		l.source = Source::Builtin;
	}
	Current() = l.source;
	return l;
}

struct Refreshed {
	bool ok = false;
	bool changed = false; // другие правила - выставлены
	std::string version;
	std::string error;
	size_t skipped = 0;
};

// Скачать правила (в своём потоке: до ~10 с на адрес) и, если годны, - в rules-github.json и в работу. Тот же файл - только
// отметка, что получен (время записи). Свой файл рядом с программой - ничего не качает.
inline Refreshed Refresh(const std::wstring& folder, const std::string& appVersion,
                         std::vector<std::wstring> addresses = {}) {
	Refreshed f;
	if (Exists(LocalPath(folder))) {
		f.error = "rules.json is next to the program";
		return f;
	}
	if (addresses.empty()) addresses = Addresses(appVersion);
	std::string body;
	for (const auto& address : addresses) {
		DWORD status = 0;
		if (!Update::Fetch(address.c_str(), nullptr, body, status, f.error)) return f;
		if (status == 200) break;
		f.error = "GitHub answered " + std::to_string(status);
		body.clear();
		if (status != 404) return f;
	}
	if (body.empty()) return f;
	Result r = Parse(body, appVersion);
	if (!r.ok) {
		f.error = r.error;
		return f;
	}
	f.version = r.version;
	f.skipped = r.skipped.size();
	std::string old;
	const bool same = ReadText(CachePath(folder), old) && old == body;
	// Через временный файл: прерванная запись не портит прежний.
	const std::wstring temp = CachePath(folder) + L".tmp";
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		out << body;
		if (!out) {
			f.error = "can't write rules-github.json";
			return f;
		}
	}
	if (!MoveFileExW(temp.c_str(), CachePath(folder).c_str(), MOVEFILE_REPLACE_EXISTING)) {
		DeleteFileW(temp.c_str());
		f.error = "can't write rules-github.json: error " + std::to_string(GetLastError());
		return f;
	}
	f.ok = true;
	if (!same || Current() != Source::Cache) {
		Rules::Set(std::move(r.lists));
		Current() = Source::Cache;
		f.changed = true;
	}
	return f;
}

}
