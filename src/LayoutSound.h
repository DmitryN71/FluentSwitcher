// Звук при смене раскладки (sound_volume): короткий звук языка новой раскладки, как у Punto Switcher, - по нему
// слышно, какая раскладка включилась. Файлы - в папке sounds рядом с программой: сначала <ru-ru>.wav, потом
// <ru>.wav, иначе other.wav; свои звуки (tools/make_sounds.py) можно заменить любыми WAV. Громкость - уровнем
// самих отсчётов (PCM 8/16 бит), остальные форматы играются как есть.
#pragma once

#include <mmsystem.h>

#include <fstream>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "winmm.lib")

class LayoutSound {
	HKL m_last = 0;
	std::map<std::wstring, std::vector<char>> m_cache; // "<файл>|<громкость>" -> WAV в памяти (играет из неё)

	static std::wstring Lower(std::wstring s) {
		for (auto& c : s) c = (wchar_t)towlower(c);
		return s;
	}

	// Громкость volume % - уменьшением отсчётов; не PCM 8/16 бит - false (тогда как есть).
	static bool Scale(std::vector<char>& wav, int volume) {
		if (wav.size() < 12 || memcmp(wav.data(), "RIFF", 4) != 0 || memcmp(wav.data() + 8, "WAVE", 4) != 0) return false;
		WORD format = 0, bits = 0;
		size_t at = 12;
		while (at + 8 <= wav.size()) {
			const char* id = wav.data() + at;
			DWORD size = 0;
			memcpy(&size, wav.data() + at + 4, 4);
			const size_t body = at + 8;
			if (body + size > wav.size()) return false;
			if (memcmp(id, "fmt ", 4) == 0 && size >= 16) {
				memcpy(&format, wav.data() + body, 2);
				memcpy(&bits, wav.data() + body + 14, 2);
			}
			else if (memcmp(id, "data", 4) == 0) {
				if (format != WAVE_FORMAT_PCM || (bits != 8 && bits != 16)) return false;
				if (bits == 16) {
					for (size_t i = body; i + 1 < body + size; i += 2) {
						short s;
						memcpy(&s, wav.data() + i, 2);
						s = (short)(s * volume / 100);
						memcpy(wav.data() + i, &s, 2);
					}
				}
				else {
					for (size_t i = body; i < body + size; i++) {
						int s = (unsigned char)wav[i] - 128;
						wav[i] = (char)(unsigned char)(s * volume / 100 + 128);
					}
				}
				return true;
			}
			at = body + size + (size & 1);
		}
		return false;
	}

	static std::wstring FileFor(HKL lay) {
		const std::wstring folder = (PathUtils::GetPath_folder_noLower2() / L"sounds").wstring() + L"\\";
		const std::wstring name = Lower(Utils::GetNameForHKL_simple(lay)); // "ru-ru"
		std::vector<std::wstring> tries{ name };
		if (auto dash = name.find(L'-'); dash != std::wstring::npos) tries.push_back(name.substr(0, dash));
		tries.push_back(L"other");
		for (const auto& t : tries) {
			const std::wstring path = folder + t + L".wav";
			if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return path;
		}
		return {};
	}

public:
	// Раскладка у окна впереди (WM_LayNotif). Первая известная - без звука; потом - при каждой смене.
	void OnLayout(HKL lay) {
		if (!lay) return;
		if (!m_last || lay == m_last) {
			m_last = lay;
			return;
		}
		m_last = lay;
		const int volume = std::clamp(conf_get_unsafe()->sound_volume, 0, 100);
		if (volume == 0) return;
		const std::wstring path = FileFor(lay);
		if (path.empty()) return;
		const std::wstring key = path + L"|" + std::to_wstring(volume);
		auto it = m_cache.find(key);
		if (it == m_cache.end()) {
			std::ifstream f(path, std::ios::binary);
			std::vector<char> wav((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			if (wav.empty()) return;
			if (volume < 100 && !Scale(wav, volume)) {
				LOG_ANY(L"sound: {} as is (not PCM 8/16 bit)", path);
				PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
				return;
			}
			it = m_cache.emplace(key, std::move(wav)).first;
		}
		LOG_ANY(L"sound: {} at {}%", path, volume);
		// Из памяти и не дожидаясь конца; следующий звук прерывает этот, буфер в кэше живёт дальше.
		PlaySoundW((LPCWSTR)it->second.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
	}

	// Настройки перечитаны: файлы могли смениться. Звук из кэша сначала останавливается - он играет из его памяти.
	void Reset() {
		PlaySoundW(nullptr, nullptr, 0);
		m_cache.clear();
	}
	~LayoutSound() { PlaySoundW(nullptr, nullptr, 0); }
};
