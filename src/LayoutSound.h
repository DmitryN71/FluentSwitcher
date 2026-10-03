// Звуки, как у Punto Switcher: короткий щелчок, когда раскладку переключили (своим сочетанием FluentSwitcher,
// сочетанием Windows, щелчком по флагу) - sound_switch, и когда FluentSwitcher исправляет текст - sound_fix.
// Переход в окно с другой раскладкой - не переключение, без звука. Файлы - в папке sounds рядом с программой:
// переключение - <ru-ru>.wav, <ru>.wav, иначе switch.wav (свой звук языка - по желанию); исправление - fix.wav.
// Любой можно заменить своим WAV. Громкость - уровнем самих отсчётов (PCM 8/16 бит), другие форматы - как есть.
#pragma once

#include <mmsystem.h>

#include <fstream>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "winmm.lib")

class LayoutSound {
	HKL m_last = 0;
	ULONGLONG m_fixAt = 0; // когда началось последнее исправление: его смена раскладки - без звука переключения
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

	// Первый из файлов names.wav, который есть в папке sounds.
	static std::wstring Find(const std::vector<std::wstring>& names) {
		const std::wstring folder = (PathUtils::GetPath_folder_noLower2() / L"sounds").wstring() + L"\\";
		for (const auto& name : names) {
			const std::wstring path = folder + name + L".wav";
			if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return path;
		}
		return {};
	}

	void Play(const std::wstring& path, int volume) {
		volume = std::clamp(volume, 0, 100);
		if (path.empty() || volume == 0) return;
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

public:
	// Раскладка у окна впереди сменилась (WM_LayNotif). Первая известная - без звука; otherWindow - перешли в окно
	// со своей раскладкой, это не переключение; сразу после начала исправления - звук был свой.
	void OnLayout(HKL lay, bool otherWindow) {
		if (!lay) return;
		if (!m_last || lay == m_last) {
			m_last = lay;
			return;
		}
		m_last = lay;
		if (otherWindow || GetTickCount64() - m_fixAt < 1500) return;
		const int volume = conf_get_unsafe()->sound_switch;
		if (volume <= 0) return;
		const std::wstring name = Lower(Utils::GetNameForHKL_simple(lay)); // "ru-ru"
		std::vector<std::wstring> names{ name };
		if (auto dash = name.find(L'-'); dash != std::wstring::npos) names.push_back(name.substr(0, dash));
		names.push_back(L"switch");
		Play(Find(names), volume);
	}

	// FluentSwitcher начинает исправлять текст (WM_TextFixed).
	void OnFix() {
		m_fixAt = GetTickCount64();
		Play(Find({ L"fix" }), conf_get_unsafe()->sound_fix);
	}

	// Настройки перечитаны: файлы могли смениться. Звук из кэша сначала останавливается - он играет из его памяти.
	void Reset() {
		PlaySoundW(nullptr, nullptr, 0);
		m_cache.clear();
	}
	~LayoutSound() { PlaySoundW(nullptr, nullptr, 0); }
};
