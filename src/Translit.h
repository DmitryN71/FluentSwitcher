#pragma once

#include <functional>
#include <string>
#include <vector>

// Выделенное транслитом (форум, xvii, 12.10.2026: "как в Punto" - имена файлов и папок латиницей): кириллица - латиницей,
// остальное как есть. Из знаков - только апостроф на месте ь и ъ в обратимой таблице (чтобы вернуть обратно); годится в
// имя файла. Русский - по выбранной таблице (Table, ниже); обратимая:
//   Русский: а-a б-b в-v г-g д-d е-e ё-e ж-zh з-z и-i й-y к-k л-l м-m н-n о-o п-p р-r с-s т-t у-u ф-f х-kh ц-ts ч-ch
//   ш-sh щ-shch ы-y э-e ю-yu я-ya, ь - апостроф, ъ - два ("Отчёт за май" - "Otchet za may", "нельзя открыть" -
//   "nel'zya otkryt'", "Объявление" - "Ob''yavlenie"): так текст возвращается обратно (ToCyrillic; Дмитрий, 13.10.2026).
//   Украинский - по официальной таблице (постановление Кабмина Украины № 55, 2010): г-h ґ-g и-y і-i, є ї й ю я в начале
//   слова - ye yi y yu ya, дальше - ie i i iu ia, зг - zgh, апостроф выпадает ("Київ" - "Kyiv", "Згорани" - "Zghorany").
// Украинский текст - по его буквам (ґ є і ї, апостроф внутри слова) или по украинской раскладке (Ukrainian).
// Прописная буква, ставшая несколькими латинскими: в слове ПРОПИСНЫМИ - все прописные ("ЩУКА" - "SHCHUKA"), иначе -
// только первая ("Щука" - "Shchuka").
namespace Translit {

inline bool Cyrillic(wchar_t c) { return c >= 0x400 && c <= 0x4FF; }
inline bool Apostrophe(wchar_t c) { return c == L'\'' || c == L'\x2019' || c == L'\x02BC'; }

inline wchar_t Lower(wchar_t c) { return (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)c); }
inline bool Upper(wchar_t c) { return IsCharUpperW(c) != FALSE; }

// Апостроф между двумя кириллическими буквами - украинский ("м'ясо"): в русском на этом месте ъ.
inline bool InnerApostrophe(const std::wstring& s, size_t i) {
	return Apostrophe(s[i]) && i > 0 && i + 1 < s.size() && Cyrillic(s[i - 1]) && Cyrillic(s[i + 1]);
}

inline bool UkrainianText(const std::wstring& s) {
	if (s.find_first_of(L"ґєіїҐЄІЇ") != std::wstring::npos) return true;
	for (size_t i = 0; i < s.size(); i++)
		if (InnerApostrophe(s, i)) return true;
	return false;
}

inline const wchar_t* Russian(wchar_t c) {
	switch (c) {
	case L'а': return L"a";
	case L'б': return L"b";
	case L'в': return L"v";
	case L'г': return L"g";
	case L'д': return L"d";
	case L'е': return L"e";
	case L'ё': return L"e";
	case L'ж': return L"zh";
	case L'з': return L"z";
	case L'и': return L"i";
	case L'й': return L"y";
	case L'к': return L"k";
	case L'л': return L"l";
	case L'м': return L"m";
	case L'н': return L"n";
	case L'о': return L"o";
	case L'п': return L"p";
	case L'р': return L"r";
	case L'с': return L"s";
	case L'т': return L"t";
	case L'у': return L"u";
	case L'ф': return L"f";
	case L'х': return L"kh";
	case L'ц': return L"ts";
	case L'ч': return L"ch";
	case L'ш': return L"sh";
	case L'щ': return L"shch";
	case L'ъ': return L"''";
	case L'ы': return L"y";
	case L'ь': return L"'";
	case L'э': return L"e";
	case L'ю': return L"yu";
	case L'я': return L"ya";
	}
	return nullptr;
}

// Таблица русского (translit_table; Дмитрий, 14.10.2026, после форума, xvii: "варианты «Википедия» и/или «Мосметро»"):
//   Reversible - своя, обратимая (Russian выше): ь и ъ - апострофы, текст возвращается обратно (ToCyrillic).
//   Wikipedia  - английской Википедии (WP:RUS, BGN/PCGN без знаков): читаемо, без апострофов - е в начале слова, после
//                гласной, ь и ъ - ye, ё - yo, ь и ъ выпадают (перед а и о у ы э - y), окончания -ий, -ый - y, -ые - ye:
//                "Объявление" - "Obyavleniye", "Достоевский" - "Dostoyevsky", "Ильинский" - "Ilyinsky". Обратно - с потерями.
//   Passport   - загранпаспорта (ICAO Doc 9303, паспорта РФ с 2014 года): ё - e, й - i, ъ - ie, ь выпадает, ю - iu,
//                я - ia: "Юлия" - "Iuliia", "Объявление" - "Obieiavlenie".
// "Мосметро" опубликованной таблицы не имеет; по цели это Wikipedia. Украинский - всегда по официальной таблице.
enum class Table { Reversible, Wikipedia, Passport };

inline bool RussianVowel(wchar_t c) { return c && wcschr(L"аеёиоуыэюя", c) != nullptr; }

// Буква s[i] (строчная - low) по таблице table; соседи - для Wikipedia. Пустая строка - буква выпадает, nullptr - не русская.
inline const wchar_t* RussianIn(Table table, const std::wstring& s, size_t i, wchar_t low) {
	if (table == Table::Reversible) return Russian(low);
	if (table == Table::Passport) {
		switch (low) {
		case L'ё': return L"e";
		case L'й': return L"i";
		case L'ъ': return L"ie";
		case L'ь': return L"";
		case L'ю': return L"iu";
		case L'я': return L"ia";
		}
		return Russian(low);
	}
	auto at = [&s](size_t j) { return j < s.size() && Cyrillic(s[j]) ? Lower(s[j]) : (wchar_t)0; };
	auto wordEnd = [&s](size_t j) { return j >= s.size() || !IsCharAlphaW(s[j]); };
	const wchar_t prev = i > 0 ? at(i - 1) : 0, next = at(i + 1);
	const bool start = i == 0 || !IsCharAlphaW(s[i - 1]);
	switch (low) {
	case L'е':
		if (prev == L'ы' && wordEnd(i + 1)) return L"e"; // -ые: Naberezhnye
		return start || RussianVowel(prev) || prev == L'ь' || prev == L'ъ' ? L"ye" : L"e";
	case L'ё': return L"yo";
	case L'и':
	case L'ы':
		if (next == L'й' && wordEnd(i + 2)) return L"y"; // -ий, -ый: Dostoyevsky, Krasny
		return low == L'и' ? L"i" : L"y";
	case L'й': return (prev == L'и' || prev == L'ы') && wordEnd(i + 1) ? L"" : L"y";
	case L'ъ':
	case L'ь': return next && RussianVowel(next) && !wcschr(L"еёюя", next) ? L"y" : L"";
	}
	return Russian(low);
}

// start - буква в начале слова; prev - буква перед ней (строчная).
inline const wchar_t* Ukrainian(wchar_t c, bool start, wchar_t prev) {
	switch (c) {
	case L'г': return prev == L'з' ? L"gh" : L"h";
	case L'ґ': return L"g";
	case L'и': return L"y";
	case L'і': return L"i";
	case L'є': return start ? L"ye" : L"ie";
	case L'ї': return start ? L"yi" : L"i";
	case L'й': return start ? L"y" : L"i";
	case L'ю': return start ? L"yu" : L"iu";
	case L'я': return start ? L"ya" : L"ia";
	case L'ь': return L""; // по официальной таблице не передаётся
	}
	return Russian(c);
}

inline std::wstring Convert(const std::wstring& s, bool ukrainian, Table table = Table::Reversible) {
	std::wstring out;
	out.reserve(s.size() + s.size() / 4);
	auto letter = [&s](size_t j) { return j < s.size() && IsCharAlphaW(s[j]); };
	for (size_t i = 0; i < s.size(); i++) {
		const wchar_t c = s[i];
		if (ukrainian && InnerApostrophe(s, i)) continue;
		if (!Cyrillic(c)) {
			out += c;
			continue;
		}
		const wchar_t low = Lower(c);
		const bool start = i == 0 || !(IsCharAlphaW(s[i - 1]) || Apostrophe(s[i - 1]));
		const wchar_t* t = ukrainian ? Ukrainian(low, start, i > 0 ? Lower(s[i - 1]) : 0) : RussianIn(table, s, i, low);
		if (!t) {
			out += c;
			continue;
		}
		std::wstring piece = t;
		if (Upper(c) && !piece.empty()) {
			const bool nextUpper = letter(i + 1) && Upper(s[i + 1]);
			const bool nextLower = letter(i + 1) && !Upper(s[i + 1]);
			const bool prevUpper = i > 0 && letter(i - 1) && Upper(s[i - 1]);
			const bool allCaps = nextUpper || (prevUpper && !nextLower);
			for (size_t k = 0; k < piece.size(); k++)
				if (k == 0 || allCaps) piece[k] = (wchar_t)towupper(piece[k]);
		}
		out += piece;
	}
	return out;
}

// Обратно, латиницу - кириллицей (Дмитрий, 13.10.2026: "ZIMA" - "ЗИМА"), когда в выделенном кириллицы нет. Таблица - та же,
// русская, и распространённые другие написания: shch zh kh ts ch sh yu ya yo ye ju ja jo - одной буквой, j - й, h - х,
// c q - к, w - в, x - кс, апостроф после согласной - ь, два - ъ (nel'zya - нельзя, otkryt' - открыть, Ob''yavlenie -
// Объявление); y после гласной - й (may - май), после согласной - ы (tsvety - цветы). Расширение файла в конце (.docx)
// остаётся латиницей. Ё не вернуть.
// Где написание неоднозначно, у слова есть варианты: ts - ц или тс (tsvet - цвет, но prigoditsya - пригодится, detskiy -
// детский), e в начале слова и после гласной - е или э (eto - это, aeroport - аэропорт). known - знает ли словарь слово
// (движок: словарь Windows, русский): первый вариант он не знает, а другой знает - другой (Дмитрий, 13.10.2026:
// "пригодиця", "меняеця"). Без словаря - первый: ц и е.
inline std::wstring ToCyrillic(const std::wstring& s, const std::function<bool(const std::wstring&)>& known = nullptr) {
	static const std::pair<const wchar_t*, const wchar_t*> table[] = {
		{ L"shch", L"щ" }, { L"zh", L"ж" }, { L"kh", L"х" }, { L"ts", L"ц" }, { L"ch", L"ч" }, { L"sh", L"ш" },
		{ L"yu", L"ю" }, { L"ya", L"я" }, { L"yo", L"ё" }, { L"ye", L"е" }, { L"ju", L"ю" }, { L"ja", L"я" },
		{ L"jo", L"ё" },
		{ L"a", L"а" }, { L"b", L"б" }, { L"v", L"в" }, { L"g", L"г" }, { L"d", L"д" }, { L"e", L"е" }, { L"z", L"з" },
		{ L"i", L"и" }, { L"j", L"й" }, { L"k", L"к" }, { L"l", L"л" }, { L"m", L"м" }, { L"n", L"н" }, { L"o", L"о" },
		{ L"p", L"п" }, { L"r", L"р" }, { L"s", L"с" }, { L"t", L"т" }, { L"u", L"у" }, { L"f", L"ф" }, { L"h", L"х" },
		{ L"c", L"к" }, { L"q", L"к" }, { L"w", L"в" }, { L"x", L"кс" },
	};
	auto latin = [](wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); };
	auto low = [](wchar_t c) { return (c >= L'A' && c <= L'Z') ? (wchar_t)(c + 32) : c; };
	auto vowel = [](wchar_t c) { return c && wcschr(L"аеёиоуыэюя", c) != nullptr; };
	// Расширение в конце: точка и до пяти латинских букв и цифр.
	size_t end = s.size();
	if (const size_t dot = s.rfind(L'.'); dot != std::wstring::npos && s.size() - dot - 1 >= 1 && s.size() - dot - 1 <= 5 &&
		std::all_of(s.begin() + dot + 1, s.end(), [&](wchar_t c) { return latin(c) || iswdigit(c); }))
		end = dot;
	auto upper = [](std::wstring& p, bool all) {
		for (size_t k = 0; k < p.size(); k++)
			if (k == 0 || all) p[k] = (wchar_t)(UINT_PTR)CharUpperW((LPWSTR)(UINT_PTR)p[k]);
	};
	// Слово - кусками: main - буква по таблице, alt - другой вариант (пусто - его нет).
	struct Piece { std::wstring main, alt; };
	std::vector<Piece> word;
	std::wstring out;
	auto flush = [&] {
		size_t choices = 0;
		for (const auto& p : word) choices += !p.alt.empty();
		auto build = [&](unsigned mask) {
			std::wstring w;
			unsigned bit = 0;
			for (const auto& p : word) w += !p.alt.empty() && (mask >> bit++ & 1) ? p.alt : p.main;
			return w;
		};
		std::wstring chosen = build(0);
		if (known && choices > 0 && choices <= 6 && !known(chosen)) {
			for (unsigned mask = 1; mask < (1u << choices); mask++)
				if (const std::wstring w = build(mask); known(w)) {
					chosen = w;
					break;
				}
		}
		out += chosen;
		word.clear();
	};
	for (size_t i = 0; i < end;) {
		const wchar_t c = s[i];
		// Апостроф после согласной - ь, два - ъ (в конце слова тоже: otkryt'); после гласной - апостроф как есть.
		if (c == L'\'' && i > 0 && latin(s[i - 1]) && !wcschr(L"aeiouy", low(s[i - 1]))) {
			const bool hard = i + 1 < end && s[i + 1] == L'\'';
			const size_t next = i + (hard ? 2 : 1);
			const bool up = s[i - 1] <= L'Z' && !(next < end && latin(s[next]) && s[next] > L'Z');
			word.push_back({ std::wstring(1, hard ? (up ? L'Ъ' : L'ъ') : (up ? L'Ь' : L'ь')), {} });
			i = next;
			continue;
		}
		if (!latin(c)) {
			flush();
			out += c;
			i++;
			continue;
		}
		size_t len = 1;
		Piece piece;
		const wchar_t prevOut = word.empty() || word.back().main.empty()
			? 0 : (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)word.back().main.back());
		if (low(c) == L'y' && !(i + 1 < end && wcschr(L"uaoe", low(s[i + 1])))) {
			piece.main = vowel(prevOut) || word.empty() ? L"й" : L"ы";
		}
		else {
			for (const auto& [from, to] : table) {
				const size_t n = wcslen(from);
				if (i + n > end) continue;
				size_t k = 0;
				while (k < n && low(s[i + k]) == from[k]) k++;
				if (k == n) {
					len = n;
					piece.main = to;
					if (wcscmp(from, L"ts") == 0) piece.alt = L"тс";
					else if (wcscmp(from, L"e") == 0 && (word.empty() || vowel(prevOut))) piece.alt = L"э";
					break;
				}
			}
		}
		if (piece.main.empty()) {
			flush();
			out += c;
			i++;
			continue;
		}
		// Регистр: первая латинская прописная - прописная; ПРОПИСНЫМИ (и следующая буква прописная) - вся прописная.
		if (c <= L'Z') {
			const bool nextUpper = i + len < end && latin(s[i + len]) && s[i + len] <= L'Z';
			const bool restUpper = std::all_of(s.begin() + i, s.begin() + i + len, [](wchar_t x) { return x <= L'Z'; });
			const bool allCaps = len > 1 ? restUpper : nextUpper || (i > 0 && latin(s[i - 1]) && s[i - 1] <= L'Z');
			upper(piece.main, allCaps);
			upper(piece.alt, allCaps);
		}
		word.push_back(std::move(piece));
		i += len;
	}
	flush();
	out.append(s, end, std::wstring::npos);
	return out;
}

}
