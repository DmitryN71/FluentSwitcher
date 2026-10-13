#pragma once

// Выделенное транслитом (форум, xvii, 12.10.2026: "как в Punto" - имена файлов и папок латиницей): кириллица - латиницей,
// остальное как есть. Без апострофов и знаков - годится в имя файла.
//   Русский: а-a б-b в-v г-g д-d е-e ё-e ж-zh з-z и-i й-y к-k л-l м-m н-n о-o п-p р-r с-s т-t у-u ф-f х-kh ц-ts ч-ch
//   ш-sh щ-shch ы-y э-e ю-yu я-ya, ь и ъ выпадают ("Отчёт за май" - "Otchet za may", "Объявление" - "Obyavlenie").
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
	case L'ъ': return L"";
	case L'ы': return L"y";
	case L'ь': return L"";
	case L'э': return L"e";
	case L'ю': return L"yu";
	case L'я': return L"ya";
	}
	return nullptr;
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
	}
	return Russian(c);
}

inline std::wstring Convert(const std::wstring& s, bool ukrainian) {
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
		const wchar_t* t = ukrainian ? Ukrainian(low, start, i > 0 ? Lower(s[i - 1]) : 0) : Russian(low);
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

}
