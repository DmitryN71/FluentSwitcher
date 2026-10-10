#include "../Localization/Russian.h"
#include "../Localization/Ukrainian.h"

namespace Localization {

	using Lookup = std::vector<std::pair<uintptr_t, const char*>>; // пока ищем прямо по адресу...

	inline Lookup Build(std::span<const std::pair<const char*, const char*>> arr) {
		Lookup lookup;
		lookup.reserve(arr.size());
		for (const auto& [key, value] : arr) {
			// Кастуем адрес строки (указатель) в число
			lookup.emplace_back(reinterpret_cast<uintptr_t>(key), value);
		}
		std::sort(lookup.begin(), lookup.end());
		return lookup;
	}

	// Язык меняется на ходу (окно настроек, gui_lang - SettingsIpc.h), а LOC читают и другие потоки (журнал
	// автопереключения): таблицы готовы заранее и не меняются, меняется только указатель на нужную.
	inline const Lookup& Table(UView language) {
		static const Lookup russian = Build(_Localization_Russian), ukrainian = Build(_Localization_Ukrainian), none;
		return language == "Russian" ? russian : language == "Ukrainian" ? ukrainian : none;
	}
	inline std::atomic<const Lookup*> active{ nullptr };

	inline void Reinit(UView language) { active = &Table(language); }

	inline const char* find(UStr s) {
		const Lookup* lookup = active.load();
		if (!lookup) return s;
		auto key = reinterpret_cast<uintptr_t>(s);
		auto it = std::lower_bound(lookup->begin(), lookup->end(), key,
			[](const auto& pair, auto val) {
				return pair.first < val;
			});

		if (it != lookup->end() && it->first == key) {
			return it->second;
		}
		return s;
	}
}

inline UStr LOC(UStr s) { return Localization::find(s); }
