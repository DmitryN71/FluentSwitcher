// Звуки, как у Punto Switcher: короткий щелчок, когда раскладку переключили (своим сочетанием FluentSwitcher,
// сочетанием Windows, щелчком по флагу) - sound_switch, и когда FluentSwitcher исправляет текст - sound_fix.
// Переход в окно с другой раскладкой - не переключение, без звука. Файлы - в папке sounds рядом с программой:
// переключение - <ru-ru>.wav, <ru>.wav, иначе switch.wav (свой звук языка - по желанию); исправление - fix.wav.
// Любой можно заменить своим WAV. Громкость - уровнем самих отсчётов (PCM 8/16 бит), другие форматы - как есть.
// Звук идёт в Bluetooth-наушники или через удалённый рабочий стол - сначала 0,2 с тишины (SlowStart).
#pragma once

#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <wrl/client.h>

#include <fstream>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "winmm.lib")

class LayoutSound {
	HKL m_last = 0;
	ULONGLONG m_fixAt = 0; // когда началось последнее исправление: его смена раскладки - без звука переключения
	std::map<std::wstring, std::vector<char>> m_cache; // "<файл>|<громкость>|<тишина>" -> WAV в памяти (играет из неё)

public:
	// Bluetooth-наушники, пока молчат, засыпают, и начало звука после паузы теряется; так же - звук удалённого рабочего
	// стола. Щелчок переключения (40 мс) пропадал целиком, звук исправления (125 мс) - через RDP слышно, в наушниках - нет
	// (форум, WinnyS, 12.10.2026; отчёт: звук запускался на каждое переключение). Тогда перед звуком - тишина, за неё
	// вывод просыпается. Устройство - вывод по умолчанию сейчас: его перечислитель BTHENUM (наушники), BTHHFENUM (гарнитура).
	// В наушниках уже что-то играет (музыка, видео) - они не спят: тишина только задержала бы щелчок. Сама тишина - 0,2 с
	// (в 1.5.8-test1 было 0,3 - Дмитрий: "щелчок слышно, но прямо с заметной задержкой"; звук исправления в 125 мс
	// пропадал целиком - значит, теряется больше 0,125 с).
	static constexpr int kWakeMs = 200;
	static const wchar_t* SlowStart() {
		if (GetSystemMetrics(SM_REMOTESESSION)) return L"a remote session";
		static const PROPERTYKEY kEnumerator = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 24 };
		using Microsoft::WRL::ComPtr;
		const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		bool bluetooth = false;
		{
			ComPtr<IMMDeviceEnumerator> devices;
			ComPtr<IMMDevice> out;
			ComPtr<IPropertyStore> props;
			PROPVARIANT v;
			PropVariantInit(&v);
			if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))) &&
			    SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &out)) &&
			    SUCCEEDED(out->OpenPropertyStore(STGM_READ, &props)) && SUCCEEDED(props->GetValue(kEnumerator, &v)) &&
			    v.vt == VT_LPWSTR && v.pwszVal)
				bluetooth = _wcsnicmp(v.pwszVal, L"BTH", 3) == 0;
			PropVariantClear(&v);
			ComPtr<IAudioMeterInformation> meter;
			float peak = 0;
			if (bluetooth && SUCCEEDED(out->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, &meter)) &&
			    SUCCEEDED(meter->GetPeakValue(&peak)) && peak > 0.0001f) {
				LOG_ANY("sound: Bluetooth, something is playing there - no silence first");
				bluetooth = false;
			}
		}
		if (SUCCEEDED(init)) CoUninitialize();
		return bluetooth ? L"Bluetooth" : nullptr;
	}

	// Тот же WAV (PCM) с тишиной ms в начале; не PCM или не разобрать - пусто.
	static std::vector<char> WithSilence(const std::vector<char>& wav, int ms) {
		if (wav.size() < 12 || memcmp(wav.data(), "RIFF", 4) != 0 || memcmp(wav.data() + 8, "WAVE", 4) != 0) return {};
		WORD format = 0, block = 0, bits = 0;
		DWORD rate = 0;
		size_t at = 12, fmt = 0, fmtSize = 0;
		while (at + 8 <= wav.size()) {
			DWORD size = 0;
			memcpy(&size, wav.data() + at + 4, 4);
			const size_t body = at + 8;
			if (body + size > wav.size()) return {};
			if (memcmp(wav.data() + at, "fmt ", 4) == 0 && size >= 16) {
				memcpy(&format, wav.data() + body, 2);
				memcpy(&rate, wav.data() + body + 4, 4);
				memcpy(&block, wav.data() + body + 12, 2);
				memcpy(&bits, wav.data() + body + 14, 2);
				fmt = body;
				fmtSize = size;
			}
			else if (memcmp(wav.data() + at, "data", 4) == 0) {
				if (!fmt || format != WAVE_FORMAT_PCM || !block || (bits != 8 && bits != 16)) return {};
				const DWORD silence = (DWORD)((ULONGLONG)rate * ms / 1000) * block;
				const DWORD data = size + silence;
				std::vector<char> res;
				auto put = [&](const void* p, size_t n) { res.insert(res.end(), (const char*)p, (const char*)p + n); };
				const DWORD riff = (DWORD)(4 + 8 + fmtSize + (fmtSize & 1) + 8 + data);
				put("RIFF", 4); put(&riff, 4); put("WAVE", 4);
				const DWORD fs = (DWORD)fmtSize;
				put("fmt ", 4); put(&fs, 4); put(wav.data() + fmt, fmtSize);
				if (fmtSize & 1) res.push_back(0);
				put("data", 4); put(&data, 4);
				res.insert(res.end(), silence, bits == 8 ? (char)0x80 : (char)0);
				put(wav.data() + body, size);
				return res;
			}
			at = body + size + (size & 1);
		}
		return {};
	}

private:
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
		const wchar_t* slow = SlowStart();
		const std::wstring key = path + L"|" + std::to_wstring(volume) + (slow ? L"|wake" : L"");
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
			if (slow) {
				if (auto padded = WithSilence(wav, kWakeMs); !padded.empty()) wav = std::move(padded);
			}
			it = m_cache.emplace(key, std::move(wav)).first;
		}
		if (slow) {
			LOG_ANY(L"sound: {} at {}%, {} ms of silence first ({})", path, volume, kWakeMs, slow);
		}
		else {
			LOG_ANY(L"sound: {} at {}%", path, volume);
		}
		// Из памяти и не дожидаясь конца; следующий звук прерывает этот, буфер в кэше живёт дальше.
		PlaySoundW((LPCWSTR)it->second.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
	}

public:
	// Раскладка у окна впереди сменилась (WM_LayNotif). Первая известная - без звука; otherWindow - перешли в окно
	// со своей раскладкой, это не переключение; сразу после начала исправления - звук был свой. excluded - окно программы,
	// где FluentSwitcher не работает ("Не работать в приложениях", окно удалённого доступа без work_in_remote): молчать
	// и там (Дмитрий, 11.10.2026: по RDP домашний FluentSwitcher щёлкал на каждое переключение в удалённом окне).
	void OnLayout(HKL lay, bool otherWindow, bool excluded = false) {
		if (!lay) return;
		if (!m_last || lay == m_last) {
			m_last = lay;
			return;
		}
		m_last = lay;
		if (otherWindow || excluded || GetTickCount64() - m_fixAt < 1500) {
			LOG_ANY("sound: no switch sound - {}", otherWindow ? "another window" : excluded ? "an excluded program"
			                                                                                 : "the fix has its own");
			return;
		}
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
