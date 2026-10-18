// Звуки, как у Punto Switcher: короткий щелчок, когда раскладку переключили (своим сочетанием FluentSwitcher,
// сочетанием Windows, щелчком по флагу) - sound_switch, и когда FluentSwitcher исправляет текст - sound_fix.
// Переход в окно с другой раскладкой - не переключение, без звука. Файлы - в папке sounds рядом с программой:
// переключение - <ru-ru>.wav, <ru>.wav, иначе switch.wav (свой звук языка - по желанию); исправление - fix.wav.
// Любой можно заменить своим WAV. Громкость - уровнем самих отсчётов (PCM 8/16 бит), другие форматы - как есть.
// Звук идёт в Bluetooth-наушники или через удалённый рабочий стол - сначала тишина (SlowStart): в наушники - sound_bluetooth_ms
// (0,2 с по умолчанию) и 0,5 с после звука (kTailMs), через удалённый стол - 0,2 с.
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
	std::map<std::wstring, std::vector<char>> m_cache; // "<файл>|<громкость>|<причина тишины>|<мс>" -> WAV в памяти (играет из неё)

public:
	// Bluetooth-наушники, пока молчат, засыпают, и начало звука после паузы теряется; так же - звук удалённого рабочего
	// стола. Щелчок переключения (40 мс) пропадал целиком, звук исправления (125 мс) - через RDP слышно, в наушниках - нет
	// (форум, WinnyS, 12.10.2026; отчёт: звук запускался на каждое переключение). Тогда перед звуком - тишина, за неё
	// вывод просыпается. Устройство - вывод по умолчанию сейчас: его перечислитель BTHENUM (наушники), BTHHFENUM (гарнитура).
	// В наушниках уже что-то играет (музыка, видео) - они не спят: тишина только задержала бы щелчок. Сама тишина - 0,2 с
	// (в 1.5.8-test1 было 0,3 - Дмитрий: "щелчок слышно, но прямо с заметной задержкой"; звук исправления в 125 мс
	// пропадал целиком - значит, теряется больше 0,125 с). Наушникам, которым её мало (WinnyS, 13.10.2026: отчёт - тишина
	// была, щелчка не слышно), - больше: sound_bluetooth_ms, окно настроек, «Звуки».
	static constexpr int kWakeMs = 200;
	// И после звука в наушники - тишина: звук кончился - Windows закрывает вывод, и то, что ещё шло по Bluetooth (это
	// десятые доли секунды), пропадает. Щелчок в 40 мс и звук исправления в 125 мс пропадали целиком при любой паузе перед
	// ними, а chord.wav и notify.wav Windows (0,6 и 1,3 с) с той же паузой 0,2 с слышно (форум, WinnyS, 17.10.2026:
	// «Поменял сами файлы – поставил штатные chord.wav и notify.wav из винды… Проблема исчезла»).
	static constexpr int kTailMs = 500;
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

	// Тот же WAV (PCM) с тишиной ms в начале и tailMs в конце; не PCM или не разобрать - пусто.
	static std::vector<char> WithSilence(const std::vector<char>& wav, int ms, int tailMs = 0) {
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
				const DWORD tail = (DWORD)((ULONGLONG)rate * tailMs / 1000) * block;
				const DWORD data = size + silence + tail;
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
				res.insert(res.end(), tail, bits == 8 ? (char)0x80 : (char)0);
				return res;
			}
			at = body + size + (size & 1);
		}
		return {};
	}

	static bool Remote(const wchar_t* slow) { return slow && wcscmp(slow, L"a remote session") == 0; }

	// Удалённый рабочий стол сжимает звук: резкий почти предельный щелчок (switch.wav: пик -0,4 дБ, начинается сразу с
	// большого отсчёта) там искажался и звучал "дико громким", а звук исправления - тише и мягче - был обычным (Дмитрий,
	// 13.10.2026, по RDP из дома, 1.5.8). Там у звука плавное начало (2 мс) и пик не выше kRemotePeak (-9 дБ от предела).
	// PCM 8/16 бит; другое - как есть (false).
	static constexpr double kRemotePeak = 0.35;
	static bool Soften(std::vector<char>& wav) {
		if (wav.size() < 12 || memcmp(wav.data(), "RIFF", 4) != 0 || memcmp(wav.data() + 8, "WAVE", 4) != 0) return false;
		WORD format = 0, block = 0, bits = 0;
		DWORD rate = 0;
		size_t at = 12;
		while (at + 8 <= wav.size()) {
			DWORD size = 0;
			memcpy(&size, wav.data() + at + 4, 4);
			const size_t body = at + 8;
			if (body + size > wav.size()) return false;
			if (memcmp(wav.data() + at, "fmt ", 4) == 0 && size >= 16) {
				memcpy(&format, wav.data() + body, 2);
				memcpy(&rate, wav.data() + body + 4, 4);
				memcpy(&block, wav.data() + body + 12, 2);
				memcpy(&bits, wav.data() + body + 14, 2);
			}
			else if (memcmp(wav.data() + at, "data", 4) == 0) {
				if (format != WAVE_FORMAT_PCM || !block || (bits != 8 && bits != 16)) return false;
				const size_t bytes = bits / 8, count = size / bytes, perFrame = block / bytes;
				auto get = [&](size_t i) -> double {
					if (bytes == 2) {
						short v;
						memcpy(&v, wav.data() + body + i * 2, 2);
						return v / 32768.0;
					}
					return ((unsigned char)wav[body + i] - 128) / 128.0;
				};
				auto put = [&](size_t i, double v) {
					v = std::clamp(v, -1.0, 32767.0 / 32768.0);
					if (bytes == 2) {
						const short s = (short)std::lround(v * 32768.0);
						memcpy(wav.data() + body + i * 2, &s, 2);
					}
					else
						wav[body + i] = (char)(unsigned char)std::lround(v * 128.0 + 128.0);
				};
				double peak = 0;
				for (size_t i = 0; i < count; i++) peak = (std::max)(peak, std::abs(get(i)));
				const double gain = peak > kRemotePeak ? kRemotePeak / peak : 1.0;
				const double fade = (std::max)(1.0, rate * 2 / 1000.0); // кадров в 2 мс
				for (size_t i = 0; i < count; i++) {
					const double frame = (double)(i / perFrame);
					put(i, get(i) * gain * (std::min)(1.0, frame / fade));
				}
				return true;
			}
			at = body + size + (size & 1);
		}
		return false;
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
		const int wake = !slow ? 0 : Remote(slow) ? kWakeMs : std::clamp(conf_get_unsafe()->sound_bluetooth_ms, 0, 3000);
		const std::wstring key = path + L"|" + std::to_wstring(volume) +
		                         (slow ? std::wstring(L"|") + slow + L"|" + std::to_wstring(wake) : L"");
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
				if (Remote(slow)) Soften(wav);
				const int tail = Remote(slow) ? 0 : kTailMs; // через удалённый стол конец не терялся
				if (auto padded = WithSilence(wav, wake, tail); (wake > 0 || tail > 0) && !padded.empty())
					wav = std::move(padded);
			}
			it = m_cache.emplace(key, std::move(wav)).first;
		}
		// Пояснения - строками в самом вызове: в отчёте для разработчика (безопасный журнал) собранные на ходу строки
		// скрываются (ревью 18.10.2026).
		if (slow && Remote(slow)) {
			LOG_ANY(L"sound: {} at {}%, {} ms of silence first ({}), softened", path, volume, wake, slow);
		}
		else if (slow) {
			LOG_ANY(L"sound: {} at {}%, {} ms of silence first ({}), {} ms after", path, volume, wake, slow, kTailMs);
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
