// Обновление по щелчку (1.5.5; форум, Merlin_Cori, 11.10.2026: "что бы программа сама качала обновления и
// устанавливала"; Дмитрий: и временный файл после установки - удалить). Только по щелчку человека: уведомление о новой
// версии или "Обновить" в окне настроек. Скачивается файл последнего выпуска с GitHub - адрес и его SHA-256 из ответа
// проверки (Update::Check: GitHub пишет сумму сам); сумма не сошлась - ничего не ставится.
//   Поставлено установщиком (рядом unins000.exe) - установщик молча (/VERYSILENT): он сам закрывает работающую копию
//     (--quit, FluentSwitcher.iss) и запускает новую с --updated.
//   Портативная - архив распаковывает tar.exe из Windows, FluentSwitcher.exe переименовывается в .old (работающий exe
//     переименовать можно, заменить - нет), новые файлы - поверх (FluentSwitcher.json и свои звуки остаются), новая копия
//     запускается с --updated, эта выходит.
// Новая копия удаляет временную папку и .old (CleanUp) - после обновления не остаётся ни установщика, ни архива.
// Только движок; окно настроек просит его (SettingsIpc.h, SimpleSwitcher.Update).
#pragma once

#include "Update.h"

#include <bcrypt.h>
#include <filesystem>

#pragma comment(lib, "bcrypt.lib")

namespace Update {

inline std::wstring TempFolder() {
	wchar_t tmp[MAX_PATH] = {};
	GetTempPathW(MAX_PATH, tmp);
	return std::wstring(tmp) + L"FluentSwitcher-update";
}

// Поставлено установщиком: его unins000.exe рядом (в архиве его нет). Запись в "Приложениях" не годится: у Дмитрия её
// не оказалось, хотя ставил установщиком (11.10.2026).
inline bool Installed(const std::wstring& folder) {
	std::error_code ec;
	return std::filesystem::is_regular_file(std::filesystem::path(folder) / L"unins000.exe", ec);
}

// Скачать url в path, сверяя SHA-256 (строчными). false - error.
inline bool Download(const std::string& url, const std::wstring& path, const std::string& sha256, std::string& error) {
	if (url.empty() || sha256.size() != 64) {
		error = "no file or no checksum in the release";
		return false;
	}
	const std::string agent = std::string("FluentSwitcher/") + FS_VERSION;
	HINTERNET net = InternetOpenA(agent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
	if (!net) {
		error = "InternetOpen: error " + std::to_string(GetLastError());
		return false;
	}
	DWORD timeout = 30000;
	InternetSetOptionW(net, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOptionW(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
	DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI;
	if (url.starts_with("https:")) flags |= INTERNET_FLAG_SECURE;
	HINTERNET file = InternetOpenUrlA(net, url.c_str(), nullptr, 0, flags, 0); // GitHub отправляет дальше - WinINet идёт сам
	if (!file) {
		error = "InternetOpenUrl: error " + std::to_string(GetLastError());
		InternetCloseHandle(net);
		return false;
	}
	DWORD status = 0, size = sizeof(status);
	HttpQueryInfoW(file, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, nullptr);
	bool ok = status == 200;
	if (!ok) error = "the server answered " + std::to_string(status);

	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	ok = ok && BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
		BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0));
	FILE* out = nullptr;
	if (ok && _wfopen_s(&out, path.c_str(), L"wb") != 0) {
		ok = false;
		error = "can't write the file";
	}
	size_t total = 0;
	char buffer[64 * 1024];
	DWORD got = 0;
	while (ok && InternetReadFile(file, buffer, sizeof(buffer), &got) && got > 0) {
		total += got;
		ok = total < (100u << 20) && fwrite(buffer, 1, got, out) == got &&
			BCRYPT_SUCCESS(BCryptHashData(hash, (PUCHAR)buffer, got, 0));
		if (!ok) error = "the download broke off";
	}
	if (out) fclose(out);
	InternetCloseHandle(file);
	InternetCloseHandle(net);
	unsigned char digest[32] = {};
	if (ok && (total == 0 || !BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0)))) {
		ok = false;
		error = "nothing downloaded";
	}
	if (hash) BCryptDestroyHash(hash);
	if (alg) BCryptCloseAlgorithmProvider(alg, 0);
	if (ok) {
		std::string hex;
		for (unsigned char b : digest) hex += std::format("{:02x}", b);
		if (hex != sha256) {
			ok = false;
			error = "the checksum does not match";
		}
	}
	if (!ok) DeleteFileW(path.c_str());
	return ok;
}

// Запустить программу и (wait) дождаться её, код выхода - в exitCode. Ждём - это консольная (tar): без окна. Иначе -
// как обычно: скрытый запуск (SW_HIDE) Windows применила бы к первому окну новой копии.
inline bool Run(const std::wstring& exe, std::wstring line, const std::wstring& dir, bool wait, DWORD* exitCode = nullptr) {
	STARTUPINFOW si{ sizeof(si) };
	PROCESS_INFORMATION pi{};
	if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, wait ? CREATE_NO_WINDOW : 0, nullptr, dir.c_str(),
		&si, &pi))
		return false;
	bool ok = true;
	if (wait) {
		ok = WaitForSingleObject(pi.hProcess, 120000) == WAIT_OBJECT_0;
		DWORD code = 1;
		GetExitCodeProcess(pi.hProcess, &code);
		if (exitCode) *exitCode = code;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return ok;
}

struct Installing {
	bool ok = false;   // новая версия ставится: установщик запущен - или новая портативная уже запущена
	bool quit = false; // этой копии - выйти (портативная; установщик закроет её сам)
	std::string error;
};

// Поставить версию из state (state.latest) в folder. В своём потоке: скачивание - секунды и больше.
inline Installing Install(const State& state, const std::wstring& folder) {
	namespace fs = std::filesystem;
	Installing r;
	const std::wstring tmp = TempFolder();
	std::error_code ec;
	fs::remove_all(tmp, ec);
	fs::create_directories(tmp, ec);
	if (Installed(folder)) {
		const std::wstring setup = tmp + L"\\FluentSwitcher-setup.exe";
		if (!Download(state.setup.url, setup, state.setup.sha256, r.error)) return r;
		if (!Run(setup, L"\"" + setup + L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-", tmp, false)) {
			r.error = "can't start the installer: error " + std::to_string(GetLastError());
			return r;
		}
		r.ok = true;
		return r;
	}
	const std::wstring zip = tmp + L"\\FluentSwitcher.zip", unpacked = tmp + L"\\files";
	if (!Download(state.zip.url, zip, state.zip.sha256, r.error)) return r;
	fs::create_directories(unpacked, ec);
	wchar_t system[MAX_PATH] = {};
	GetSystemDirectoryW(system, MAX_PATH);
	const std::wstring tar = std::wstring(system) + L"\\tar.exe";
	DWORD code = 1;
	if (!Run(tar, L"\"" + tar + L"\" -xf \"" + zip + L"\" -C \"" + unpacked + L"\"", tmp, true, &code) || code != 0) {
		r.error = "can't unpack the archive (tar.exe, code " + std::to_string(code) + ")";
		return r;
	}
	const fs::path from = fs::path(unpacked) / L"FluentSwitcher", dir = folder;
	const fs::path exe = dir / L"FluentSwitcher.exe", old = dir / L"FluentSwitcher.exe.old";
	if (!fs::is_regular_file(from / L"FluentSwitcher.exe", ec)) {
		r.error = "no FluentSwitcher.exe in the archive";
		return r;
	}
	fs::remove(old, ec);
	if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		r.error = "can't rename FluentSwitcher.exe: error " + std::to_string(GetLastError());
		return r;
	}
	auto restore = [&] {
		fs::remove(exe, ec);
		MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
	};
	for (const auto& entry : fs::directory_iterator(from, ec)) {
		fs::copy(entry.path(), dir / entry.path().filename(),
			fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
		if (ec) {
			r.error = "can't copy " + entry.path().filename().string() + ": " + ec.message();
			restore();
			return r;
		}
	}
	if (!Run(exe.wstring(), L"\"" + exe.wstring() + L"\" --updated", dir.wstring(), false)) {
		r.error = "can't start the new FluentSwitcher.exe: error " + std::to_string(GetLastError());
		restore();
		return r;
	}
	r.ok = r.quit = true;
	return r;
}

// Временная папка обновления и FluentSwitcher.exe.old - удалить. При каждом запуске и потом раз в минуту: установщик или
// прежняя копия могут ещё работать - тогда удалится в следующий раз.
inline void CleanUp(const std::wstring& folder) {
	std::error_code ec;
	std::filesystem::remove_all(TempFolder(), ec);
	std::filesystem::remove(std::filesystem::path(folder) / L"FluentSwitcher.exe.old", ec);
}

}
