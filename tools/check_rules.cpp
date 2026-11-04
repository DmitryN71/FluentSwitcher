// Проверка файлов правил (rules/rules.json, rules/rules-beta.json) перед публикацией - в каждой сборке на GitHub
// (.github/workflows/build.yml) и перед копированием в ветку rules (publish-rules.yml):
//   check_rules.exe <папка списков слов: src\data> <файл> [<файл>...]
// Ошибка (код 1):
//   - файл не разбирается (RulesFile::Parse) или в нём слово, которое программа пропустит (не тот алфавит, прописные,
//     длина) - min_app здесь не мешает: файл может быть для версии новее этой сборки;
//   - слово сразу в add и remove одного списка; слово в short и keep одного языка;
//   - сокращение, которое в другой раскладке - частое слово: short_en "ghb" - те же клавиши в русской раскладке "при",
//     short_ru "руд" - "hel"; на компьютере пользователя от ложного переключения бережёт словарь, но не всегда;
//   - "не слово" (not_word_ru), которое на деле частое русское слово.
// Предупреждение (код 0): добавленное уже встроено, удаляемого нет во встроенных, сокращение - частое слово своего языка.
// Частые слова - по спискам программы (ShortWords.h, src\data\words_*_common.txt): словарей Windows на сборке GitHub нет.
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "RulesFile.h"
#include "ShortWords.h"
#include "WordStart.h"

static int errors = 0, warnings = 0;

static std::string U8(const std::wstring& w) {
	std::string s(WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), (int)s.size(), nullptr, nullptr);
	return s;
}

static void Error(const std::string& file, const std::string& what) {
	errors++;
	printf("error: %s: %s\n", file.c_str(), what.c_str());
}

static void Warning(const std::string& file, const std::string& what) {
	warnings++;
	printf("warning: %s: %s\n", file.c_str(), what.c_str());
}

// Те же клавиши в другой раскладке (США - русская, буквы и знаки, на которых в русской буквы).
static const wchar_t kEn[] = L"qwertyuiop[]asdfghjkl;'zxcvbnm,.`";
static const wchar_t kRu[] = L"йцукенгшщзхъфывапролджэячсмитьбюё";

static std::wstring Keys(const std::wstring& w, const wchar_t* from, const wchar_t* to) {
	std::wstring out = w;
	for (wchar_t& c : out)
		if (const wchar_t* p = wcschr(from, c)) c = to[p - from];
	return out;
}

// Частое слово языка: короткие - ShortWords, длиннее - список частых слов.
static bool Frequent(const std::wstring& w, const wchar_t* language) {
	return ShortWords::Frequent(w, language) || WordStart::IsWord(w, language, true);
}

static void Check(const char* path) {
	const std::string file = path;
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		Error(file, "can't read");
		return;
	}
	const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	const auto r = RulesFile::Parse(text, "999.0.0");
	if (!r.ok) {
		Error(file, r.error);
		return;
	}
	for (const auto& s : r.skipped) Error(file, "left out by the program: " + s);
	const auto json = nlohmann::json::parse(text, nullptr, false);
	const auto& lists = json["lists"];
	for (auto it = lists.begin(); it != lists.end(); ++it)
		if (std::ranges::none_of(RulesFile::kLists, [&](const RulesFile::ListInfo& l) { return it.key() == l.key; }))
			Error(file, "unknown list " + it.key());
	const Rules::Lists builtin = Rules::Builtin();
	auto words = [&](const char* key, const char* what) {
		std::vector<std::wstring> out;
		if (!lists.contains(key) || !lists[key].is_object() || !lists[key].contains(what)) return out;
		for (const auto& e : lists[key][what])
			if (e.is_string()) out.push_back(RulesFile::Wide(e.get<std::string>()));
		return out;
	};
	for (const RulesFile::ListInfo& info : RulesFile::kLists) {
		const auto add = words(info.key, "add"), remove = words(info.key, "remove");
		const Rules::Words& inBuiltin = builtin.*info.words;
		for (size_t i = 0; i < add.size(); i++) {
			const std::wstring& w = add[i];
			if (std::find(add.begin(), add.begin() + i, w) != add.begin() + i) continue; // о повторе - один раз
			const std::string name = std::string(info.key) + " " + U8(w);
			if (std::ranges::count(add, w) > 1) Error(file, name + ": twice in add");
			if (std::ranges::find(remove, w) != remove.end()) Error(file, name + ": in add and in remove");
			if (inBuiltin.contains(w)) Warning(file, name + ": built in already");
		}
		for (const auto& w : remove)
			if (!inBuiltin.contains(w)) Warning(file, std::string(info.key) + " " + U8(w) + ": removed, but not built in");
	}
	// Списки после файла: что получит программа.
	const Rules::Lists& l = r.lists;
	for (const auto& w : l.shortEn)
		if (l.keepEn.contains(w)) Error(file, "short_en and keep_en both have " + U8(w));
	for (const auto& w : l.shortRu)
		if (l.keepRu.contains(w)) Error(file, "short_ru and keep_ru both have " + U8(w));
	for (const auto& w : words("short_en", "add")) {
		const std::wstring other = Keys(w, kEn, kRu);
		if (Frequent(other, L"ru-RU")) Error(file, "short_en " + U8(w) + ": in the Russian layout it is the frequent " + U8(other));
		if (Frequent(w, L"en-US")) Warning(file, "short_en " + U8(w) + ": a frequent English word");
	}
	for (const auto& w : words("short_ru", "add")) {
		const std::wstring other = Keys(w, kRu, kEn);
		if (Frequent(other, L"en-US")) Error(file, "short_ru " + U8(w) + ": in the English layout it is the frequent " + U8(other));
		if (Frequent(w, L"ru-RU")) Warning(file, "short_ru " + U8(w) + ": a frequent Russian word");
	}
	for (const auto& w : words("not_word_ru", "add"))
		if (ShortWords::Band(w, L"ru-RU") > 0 || WordStart::IsWord(w, L"ru-RU", true))
			Error(file, "not_word_ru " + U8(w) + ": a frequent Russian word");
	printf("%s: version %s, %zu added, %zu removed\n", path, r.version.c_str(), r.added, r.removed);
}

int main(int argc, char** argv) {
	SetConsoleOutputCP(CP_UTF8);
	if (argc < 3) {
		printf("check_rules.exe <src\\data> <rules.json> [...]\n");
		return 2;
	}
	const std::string data = argv[1];
	WordStart::impl::dataDir = std::wstring(data.begin(), data.end());
	if (!WordStart::Available(L"ru-RU") || !WordStart::Available(L"en-US")) {
		printf("error: no word lists in %s\n", argv[1]);
		return 2;
	}
	for (int i = 2; i < argc; i++) Check(argv[i]);
	printf(errors ? "\n%d error(s), %d warning(s)\n" : "\nok, %d error(s), %d warning(s)\n", errors, warnings);
	return errors ? 1 : 0;
}
