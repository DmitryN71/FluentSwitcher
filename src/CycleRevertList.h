#pragma once

// Класс текущих набранных символов, разбивает буквы на слова.

class CycleRevertList {

	struct TKeyHookInfo {
		TKeyBaseInfo key;
		bool is_last_revert = false;
		HKL lay = 0; // раскладка, в которой клавиша сейчас на экране (автопереключение: слова перед словом)
	};

	// static const int c_maxWordRevert = 15; // https://github.com/Aegel5/SimpleSwitcher/issues/95
	static const int c_nMaxLettersSave = 90;
	std::deque<TKeyHookInfo> m_symbolList; // просто список всего, что сейчас набрано.
	static const int c_lastCorrectedInf = 9999999;
	int iLastCorrected = c_lastCorrectedInf;
	TimePoint lastadd;
	size_t m_total = 0; // клавиш добавлено минус стёрто, с начала работы: сколько набрано после какого-то момента
	size_t m_changes = 0; // клавиш добавлено и стёрто, с начала работы: изменилось ли набранное после какого-то момента

public:
	void DeleteLastSymbol() {
		if (!m_symbolList.empty()) {
			bool move_last = m_symbolList.back().is_last_revert;
			m_symbolList.pop_back();
			m_total--;
			m_changes++;
			if (move_last && !m_symbolList.empty())
				m_symbolList.back().is_last_revert = true;
		}
		ClearGenerated();
	}
	bool HasAnySymbol() const { return !m_symbolList.empty(); }
	void Clear() {
		if (!m_symbolList.empty()) {
			LOG_ANY(L"ClearsKeys {}", m_symbolList.size());
			m_symbolList.clear();
		}
		ClearGenerated();
	}
	void ClearByTimer() {
		if (lastadd.DeltToNow() >= 10min) {
			Clear();
		}
	}
private: void ClearGenerated() {
	iLastCorrected = c_lastCorrectedInf;
}

private: std::vector<int> GenerateWords(HotKeyType typeRevert) {

	// сначала объеденим все одинаковые сохраняя индексы старта слов.

	struct ZippData { int i; const TKeyHookInfo* p; TKeyType type; bool is_last_revert = false; bool digits = false; };
	std::vector<ZippData> zipped;
	zipped.reserve(8);
	{
		TKeyHookInfo stumb;
		TKeyHookInfo* prev = &stumb;
		for (int i = 0; i < m_symbolList.size(); prev = &m_symbolList[i], i++) {
			const auto& cur = m_symbolList[i];
			auto type = cur.key.type;

			if (prev->is_last_revert) { 
				zipped.emplace_back(i, &cur, type, true);
				continue;
			}

			auto check = [&]() -> bool {

				if (cur.key == prev->key) { // одинаковый клавиши - всегда обрабатываются одинаково.
					return false;
				}

				if(Utils::is_in(type, KEYTYPE_CUSTOM, KEYTYPE_LETTER_OR_CUSTOM)) return true;

				return type != prev->key.type;
				};
			if (check()) {
				zipped.emplace_back(i, &cur, type);
			}
		}
	}

	// по сути, все уже готово, осталось лишь решить вопрос possible letter / letter.

	std::vector<int> starts; // индексы в zipped, с которых начинаются слова.

	GETCONF;
	const auto can_separate_posible =
		(typeRevert == hk_RevertLastWord && cfg->separate_ext_mode == SeparateExtMode::PossibleSymb_Always)
		|| (typeRevert == hk_RevertSeveralWords && Utils::is_in(cfg->separate_ext_mode, SeparateExtMode::PossibleSymb_SeveralW, SeparateExtMode::PossibleSymb_Always));


	for (int i = -1; auto & it : zipped) {
		i++;
		auto check = [&]() -> bool {
			// it.type должен иметь актуальный тип, так как используется на следующих итерациях.
			if (can_separate_posible && it.p->key.space_on_extended) {
				it.type = KEYTYPE_SPACE;
			}
			if (it.type == KEYTYPE_LETTER_OR_SPACE) {
				it.digits = !it.p->key.space_on_extended;
				// не разделяем слово без надобности.
				it.type = (i > 0 && i < std::ssize(zipped) - 1 && Utils::is_all(KEYTYPE_LETTER, zipped[i - 1].type, zipped[i + 1].type))
					? KEYTYPE_LETTER
					: KEYTYPE_SPACE;
			}
			if (it.type == KEYTYPE_SPACE) return false;
			if (it.type == KEYTYPE_LETTER_OR_CUSTOM) {
				bool separate = can_separate_posible;
				if (separate) {
					// выделаем только если слева или справа space
					separate = i == 0 || i + 1 >= zipped.size() 
						|| Utils::is_in(KEYTYPE_SPACE, zipped[i - 1].type, zipped[i + 1].type)
						|| Utils::is_in(KEYTYPE_CUSTOM, zipped[i - 1].type, zipped[i + 1].type)
						;
				}

				it.type = separate ? KEYTYPE_CUSTOM : KEYTYPE_LETTER; // теперь можем определить тип
			}
			if (it.is_last_revert) { // форсируем разделение.
				return true;
			}
			if (it.type == KEYTYPE_LETTER) {
				return i == 0 || zipped[i - 1].type != KEYTYPE_LETTER;
			}
			return true; // custom
		};
		if (check()) {
			starts.push_back(i);
		}

	}

	// Знаки в конце слова, без пробела ("cnjg?" -> "стоп,"), относятся к этому слову. Иначе "последнее
	// слово" - один знак, и исправлялся только он. Знак между буквами ("ghbdtn.rfr") по-прежнему
	// разделяет слова. Цифры в конце слова - его часть и здесь: "КС1." исправлялось в "КС1/" - цифра считалась
	// пробелом, и знак за ней был отдельным словом (Дмитрий, 08.10.2026).
	auto word_before = [&](int z) {
		if (z <= 0) return false;
		if (Utils::is_in(zipped[z - 1].type, KEYTYPE_LETTER, KEYTYPE_CUSTOM)) return true;
		return zipped[z - 1].digits && z >= 2 && zipped[z - 2].type == KEYTYPE_LETTER;
	};
	auto only_signs_till_space = [&](int z) {
		for (; z < std::ssize(zipped) && zipped[z].type != KEYTYPE_SPACE; ++z) {
			if (zipped[z].type != KEYTYPE_CUSTOM) return false;
		}
		return true;
	};
	std::vector<int> words; // индексы старта слов.
	for (int z : starts) {
		const auto& it = zipped[z];
		bool glue = it.type == KEYTYPE_CUSTOM && !it.is_last_revert && word_before(z)
			&& only_signs_till_space(z);
		if (!glue) {
			words.push_back(it.i);
		}
	}



	//if (words.size() > c_maxWordRevert) {
	// 	std::reverse(words.begin(), words.end());
	//	words.resize(c_maxWordRevert);
	// 	std::reverse(words.begin(), words.end());
	//}

	return words;

}


struct RevertKeysData {
	TKeyRevert keys;
	bool needLanguageChange = false;
};

public: RevertKeysData FillKeyToRevert(HotKeyType typeRevert, bool always_full_text = false) {

	RevertKeysData keyList;

	if (m_symbolList.empty()) {
		LOG_WARN(L"empty m_symbolList");
		return keyList;
	}

	auto words = GenerateWords(typeRevert);

	if (words.empty()) { // например одни пробелы были введены.
		return keyList;
	}

	/*
* Новый экспериментальный алгоритм:
* храним индекс первой буквы последнего изменнного слова.
* если запрос на изменение последнего слова - просто меняем последнее слово.
* если запрос "изменить все" - меняем все.
* если запрос "несколько слов" - то ищем слово, начинающееся с индекса < сохраненного, если такого нет - то меняем последнее слово.
*/
	int prev_correct = iLastCorrected;

	if (always_full_text) {
		iLastCorrected = words[0];
		keyList.needLanguageChange = true;
	}
	else if (typeRevert == hk_RevertAllRecentText) {
		keyList.needLanguageChange = true;
		iLastCorrected = words[0];
		for (int i = ssize(m_symbolList) - 2; i >= 0; --i) {
			if (m_symbolList[i].is_last_revert) {
				iLastCorrected = i+1;
				break;
			}
		}
	}
	else if (typeRevert == hk_RevertLastWord) {
		keyList.needLanguageChange = true;
		iLastCorrected = words[words.size() - 1];
	}
	else { // несколько слов
		if (words[0] >= iLastCorrected) {
			iLastCorrected = c_lastCorrectedInf; // все слова уже изменили, сбрасываем на начало.
		}
		keyList.needLanguageChange = iLastCorrected == c_lastCorrectedInf;
		for (int i = words.size() - 1; i >= 0; i--) {
			if (words[i] < iLastCorrected) {
				iLastCorrected = words[i];
				break;
			}
		}
	}

	for (int i = always_full_text ? 0 : iLastCorrected; i < ssize(m_symbolList); ++i) {
		keyList.keys.push_back(m_symbolList[i].key);
	}

	if (prev_correct == iLastCorrected) {
		iLastCorrected = c_lastCorrectedInf; // происходит отмена предыдущего реверт, сбрасываемся на начало.
	}

	return keyList;
}
public: size_t Size() const { return m_symbolList.size(); }
// Клавиши слова перед пробелом, которым кончается набранное (ДВе ЗАглавные), - до прошлого пробела.
public: std::vector<TKeyBaseInfo*> LastWordKeys() {
	std::vector<TKeyBaseInfo*> keys;
	if (m_symbolList.size() < 2 || m_symbolList.back().key.type != KEYTYPE_SPACE) return keys;
	for (int i = (int)m_symbolList.size() - 2; i >= 0; --i) {
		if (m_symbolList[i].key.type == KEYTYPE_SPACE) break;
		keys.push_back(&m_symbolList[i].key);
	}
	std::reverse(keys.begin(), keys.end());
	return keys;
}
// Последнее набранное - граница слова: пробел или знак, одинаковый во всех раскладках (AnalyzeTyped: "!", ")").
public: bool EndsWithBoundary() const {
	return !m_symbolList.empty() && m_symbolList.back().key.type == KEYTYPE_SPACE;
}
// Клавиши слова в самом конце набранного (за ним ещё ничего): Enter / Tab придержан и в буфер не попал.
public: std::vector<TKeyBaseInfo*> TrailingWordKeys() {
	std::vector<TKeyBaseInfo*> keys;
	for (int i = (int)m_symbolList.size() - 1; i >= 0; --i) {
		if (m_symbolList[i].key.type == KEYTYPE_SPACE) break;
		keys.push_back(&m_symbolList[i].key);
	}
	std::reverse(keys.begin(), keys.end());
	return keys;
}
public: void SetSeparateLast() {
	if (!m_symbolList.empty())
		m_symbolList.back().is_last_revert = true;
}
public: void AddKeyToList(const TKeyBaseInfo& key, HKL lay = 0) {
	ClearGenerated();

	lastadd.SetToNow();

	while (m_symbolList.size() >= c_nMaxLettersSave) {
		m_symbolList.pop_front();
	}

	m_symbolList.push_back({ .key = key, .lay = lay });
	m_total++;
	m_changes++;
}

// ----- Слова в конце набранного - автопереключению (AutoSwitch.h): контекст слова и короткие слова перед ним -----
public: struct TailWord {
	size_t begin = 0, end = 0; // клавиши [begin, end)
	HKL lay = 0;               // раскладка всех его клавиш; 0 - разные или неизвестна
};
// Слова с конца набранного, через пробелы (по одному между словами): [0] - последнее (перед пробелом в самом конце, если
// afterSpace), дальше - слова перед ним; не больше max. Останавливается на исправленном слове (отметка SetSeparateLast
// на пробеле после него или в нём): оно уже в нужной раскладке - тогда fixedBefore.
public: std::vector<TailWord> TailWords(bool afterSpace, size_t max, bool* fixedBefore = nullptr) const {
	std::vector<TailWord> words;
	if (fixedBefore) *fixedBefore = false;
	int i = (int)m_symbolList.size() - 1;
	if (afterSpace) {
		if (i < 0 || m_symbolList[i].key.type != KEYTYPE_SPACE) return words;
		i--;
	}
	while (i >= 0 && words.size() < max) {
		const int end = i + 1;
		bool marked = false;
		while (i >= 0 && m_symbolList[i].key.type != KEYTYPE_SPACE) {
			marked = marked || m_symbolList[i].is_last_revert;
			i--;
		}
		const int begin = i + 1;
		if (begin == end) break; // два пробела подряд или начало набранного
		if (!words.empty() && marked) {
			if (fixedBefore) *fixedBefore = true;
			break;
		}
		TailWord word{ (size_t)begin, (size_t)end, m_symbolList[begin].lay };
		for (int k = begin; k < end; k++)
			if (m_symbolList[k].lay != word.lay) word.lay = 0;
		words.push_back(word);
		if (i < 0) break;
		if (m_symbolList[i].key.vk_code != VK_SPACE) break; // Tab - другое поле или ячейка
		if (m_symbolList[i].is_last_revert) { // пробел после слова перед ним: то исправляли
			if (fixedBefore) *fixedBefore = true;
			break;
		}
		i--;
	}
	return words;
}
public: const TKeyBaseInfo& KeyAt(size_t i) const { return m_symbolList[i].key; }
// Клавиши с begin до конца набранного.
public: TKeyRevert KeysFrom(size_t begin) const {
	TKeyRevert keys;
	for (size_t i = begin; i < m_symbolList.size(); i++) keys.push_back(m_symbolList[i].key);
	return keys;
}
// Клавиши с begin до конца перепечатаны в раскладке lay.
public: void SetLayFrom(size_t begin, HKL lay) {
	for (size_t i = begin; i < m_symbolList.size(); i++) m_symbolList[i].lay = lay;
}
public: size_t Total() const { return m_total; }
// Растёт и от набора, и от Backspace: то же число - после этого момента ничего не набирали и не стирали (Total() того не
// скажет: стёрли 4 клавиши, набрали 4 другие - он тот же).
public: size_t Changes() const { return m_changes; }

};
