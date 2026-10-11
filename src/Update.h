// Проверка новой версии, как у FluentClipper: программа спрашивает у GitHub номер последнего выпуска и больше
// ничего не отправляет. Сама ничего не скачивает и не ставит - только сообщает и открывает страницу выпуска.
// Движок проверяет раз в день и показывает уведомление у часов (gui2/main.cpp), окно настроек - по кнопке
// "Проверить сейчас" (settings/src/pages.cpp). Что нашла последняя проверка - в update.json рядом с программой.
// Без wxWidgets: нужен и движку, и окну настроек.
#pragma once

#include <windows.h>
#include <wininet.h>

#include <chrono>
#include <fstream>
#include <string>

#include "json.hpp"
#include "fs_version.h"
#include "OpenAsUser.h"

#pragma comment(lib, "wininet.lib")

namespace Update {

inline const wchar_t kReleasesPage[] = L"https://github.com/DmitryN71/FluentSwitcher/releases";
inline const wchar_t kLatestApi[] = L"https://api.github.com/repos/DmitryN71/FluentSwitcher/releases/latest";
inline const long long kDayMs = 24LL * 60 * 60 * 1000;

// Файл выпуска на GitHub: откуда скачать и его SHA-256 (GitHub пишет его сам, "digest").
struct Asset {
	std::string url;    // https://github.com/.../releases/download/v1.5.5/FluentSwitcher-1.5.5-setup.exe
	std::string sha256; // строчными
};

struct Result {
	bool ok = false;
	std::string latest; // "1.2.0"
	std::wstring page;  // страница этого выпуска
	Asset setup, zip;   // установщик и архив (обновление по щелчку - Install)
	std::string error;  // для журнала, когда не ok
};

// "1.0.10" новее "1.0.9". Бета старше выпуска с тем же номером: "1.5.0" новее "1.5.0-beta1", "1.5.0-beta2" новее
// "1.5.0-beta1" (иначе "1.5.0-beta1" читалось как 1.5.01 = 1.5.1, и бете не предлагался выпуск 1.5.0).
inline bool IsNewer(const std::string& latestFull, const std::string& currentFull) {
	auto number = [](const std::string& v) { return v.substr(0, v.find('-')); };
	auto suffix = [](const std::string& v) { return v.find('-') == std::string::npos ? std::string() : v.substr(v.find('-') + 1); };
	// Числа через точку; в суффиксе ("beta2") - его число.
	auto compare = [](const std::string& latest, const std::string& current) {
		size_t a = 0, b = 0;
		while (a < latest.size() || b < current.size()) {
			long x = 0, y = 0;
			while (a < latest.size() && latest[a] != '.') {
				if (latest[a] >= '0' && latest[a] <= '9') x = x * 10 + (latest[a] - '0');
				a++;
			}
			while (b < current.size() && current[b] != '.') {
				if (current[b] >= '0' && current[b] <= '9') y = y * 10 + (current[b] - '0');
				b++;
			}
			if (x != y) return x > y ? 1 : -1;
			a++;
			b++;
		}
		return 0;
	};
	const int numbers = compare(number(latestFull), number(currentFull));
	if (numbers != 0) return numbers > 0;
	const std::string latestSuffix = suffix(latestFull), currentSuffix = suffix(currentFull);
	if (latestSuffix.empty() != currentSuffix.empty()) return latestSuffix.empty(); // выпуск новее своей беты
	return compare(latestSuffix, currentSuffix) > 0;
}

// Спрашивает GitHub сейчас; ждёт до ~10 с на каждый шаг (нет сети, медленный прокси).
inline Result Check() {
	Result r;
	const std::string agent = std::string("FluentSwitcher/") + FS_VERSION;
	// Настройки прокси - системные, как у браузера.
	HINTERNET net = InternetOpenA(agent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
	if (!net) {
		r.error = "InternetOpen: error " + std::to_string(GetLastError());
		return r;
	}
	DWORD timeout = 10000;
	InternetSetOptionW(net, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOptionW(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOptionW(net, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
	HINTERNET url = InternetOpenUrlW(net, kLatestApi, L"Accept: application/vnd.github+json\r\n", (DWORD)-1,
		INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI |
		INTERNET_FLAG_SECURE, 0);
	if (!url) {
		r.error = "InternetOpenUrl: error " + std::to_string(GetLastError());
		InternetCloseHandle(net);
		return r;
	}
	DWORD status = 0, size = sizeof(status);
	HttpQueryInfoW(url, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, nullptr);
	std::string body;
	char buffer[8192];
	DWORD got = 0;
	while (InternetReadFile(url, buffer, sizeof(buffer), &got) && got > 0 && body.size() < (1u << 20))
		body.append(buffer, got);
	InternetCloseHandle(url);
	InternetCloseHandle(net);
	if (status != 200) {
		r.error = "GitHub answered " + std::to_string(status);
		return r;
	}
	const auto json = nlohmann::json::parse(body, nullptr, false);
	std::string tag;
	if (json.is_object()) {
		auto it = json.find("tag_name");
		if (it != json.end() && it->is_string())
			tag = it->get<std::string>();
	}
	r.latest = !tag.empty() && (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag;
	r.page = std::wstring(kReleasesPage) + L"/tag/" + std::wstring(tag.begin(), tag.end()); // метки - ASCII
	// Установщик и архив этого выпуска - с их суммами; без суммы файл не годится для обновления по щелчку.
	if (json.is_object() && json.contains("assets") && json["assets"].is_array())
		for (const auto& a : json["assets"]) {
			if (!a.is_object()) continue;
			const std::string name = a.value("name", std::string()), url = a.value("browser_download_url", std::string());
			std::string digest = a.value("digest", std::string());
			if (url.empty() || !digest.starts_with("sha256:")) continue;
			digest = digest.substr(7);
			for (auto& c : digest) c = (char)tolower((unsigned char)c);
			if (name.ends_with("-setup.exe")) r.setup = { url, digest };
			else if (name.ends_with(".zip")) r.zip = { url, digest };
		}
	r.ok = !r.latest.empty();
	if (!r.ok)
		r.error = "no tag_name in the answer";
	return r;
}

inline long long NowMs() {
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Что нашла последняя проверка (update.json рядом с программой).
struct State {
	long long checkedAt = 0; // когда GitHub ответил, мс от 1970
	std::string latest;      // номер последней версии
	std::wstring page;       // её страница
	bool failed = false;     // "Проверить сейчас" не дозвалась GitHub
	std::string notified;    // о какой версии уже было уведомление у часов
	Asset setup, zip;        // её установщик и архив
};

inline std::wstring StatePath(const std::wstring& folder) { return folder + L"\\update.json"; }

inline State Load(const std::wstring& folder) {
	State s;
	std::ifstream f(StatePath(folder), std::ios::binary);
	if (!f)
		return s;
	const auto json = nlohmann::json::parse(f, nullptr, false);
	if (!json.is_object())
		return s;
	try {
		s.checkedAt = json.value("checked_at", 0LL);
		s.latest = json.value("latest", std::string());
		const std::string page = json.value("page", std::string());
		s.page.assign(page.begin(), page.end()); // адрес - ASCII
		s.failed = json.value("failed", false);
		s.notified = json.value("notified", std::string());
		s.setup = { json.value("setup_url", std::string()), json.value("setup_sha256", std::string()) };
		s.zip = { json.value("zip_url", std::string()), json.value("zip_sha256", std::string()) };
	}
	catch (const std::exception&) {
		return State{};
	}
	return s;
}

inline bool Save(const std::wstring& folder, const State& s) {
	std::string page;
	for (wchar_t c : s.page) page += (char)c; // адрес - ASCII
	nlohmann::json json = {
		{ "checked_at", s.checkedAt },
		{ "latest", s.latest },
		{ "page", page },
		{ "failed", s.failed },
		{ "notified", s.notified },
		{ "setup_url", s.setup.url },
		{ "setup_sha256", s.setup.sha256 },
		{ "zip_url", s.zip.url },
		{ "zip_sha256", s.zip.sha256 },
	};
	std::ofstream f(StatePath(folder), std::ios::binary | std::ios::trunc);
	if (!f)
		return false;
	f << json.dump(2);
	return (bool)f;
}

// Ответ проверки - в State. Неудачная автоматическая проверка ничего не меняет (повторится позже),
// неудачная "Проверить сейчас" запоминается: окно настроек предложит открыть страницу в браузере.
inline void Apply(State& s, const Result& r, bool manual) {
	if (r.ok) {
		s.checkedAt = NowMs();
		s.latest = r.latest;
		s.page = r.page;
		s.failed = false;
		s.setup = r.setup;
		s.zip = r.zip;
	}
	else if (manual) {
		s.failed = true;
	}
}

inline bool NewerKnown(const State& s) { return !s.latest.empty() && IsNewer(s.latest, FS_VERSION); }

}
