#pragma once
#include <fstream>
#include <clocale>
#include "Errors.h"
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
#include <source_location>
#include <print>
#include <csignal>
#include <filesystem>
#include <tuple>
#include <vector>

enum TLogLevel {
	LOG_LEVEL_DISABLE = 0,  
	LOG_LEVEL_1 = 1,
	LOG_LEVEL_2 = 2,
	LOG_LEVEL_3 = 3,
	LOG_LEVEL_4 = 4,
};

namespace _log_int {
	inline constinit std::atomic<TLogLevel> log_level = TLogLevel::LOG_LEVEL_DISABLE;
}

inline TLogLevel GetLogLevel() { return _log_int::log_level;}
inline void SetLogLevel(TLogLevel val) { _log_int::log_level = val; }

// Безопасный журнал - "Отчёт для разработчика" (1.5.2; Дмитрий, 11.10.2026: "debug log, который они смогут включать, не
// опасаясь, что пароли и т.д. в логе будут"). Набранного текста в нём нет ни в каком виде: пароль не везде виден как
// пароль (браузер, игра, удалённый стол). Строки в значениях записей по умолчанию закрыты - вместо них число символов,
// "‹6›". Открыты: строки-константы программы (они в её памяти только для чтения - InReadOnlyImage), числа и LogPlain(...) -
// то, что проверено: имена программ и классов окон, причины решений, версии, клавиши управления (LogKey, LogHotKey в
// CHotKey.h). Клавиши текста - буквы, цифры, знаки - без названия и кода. Пропущенная строка закрыта, а не открыта.
namespace _log_int {
	inline constinit std::atomic<bool> safe = false;
}
inline bool LogSafe() { return _log_int::safe; }

// Проверенное значение - в безопасный журнал как есть.
struct LogPlainW { std::wstring s; };
struct LogPlainA { std::string s; };
inline LogPlainW LogPlain(std::wstring s) { return { std::move(s) }; }
inline LogPlainW LogPlain(std::wstring_view s) { return { std::wstring(s) }; }
inline LogPlainW LogPlain(const wchar_t* s) { return { s ? s : L"" }; }
inline LogPlainA LogPlain(std::string s) { return { std::move(s) }; }
inline LogPlainA LogPlain(std::string_view s) { return { std::string(s) }; }
inline LogPlainA LogPlain(const char* s) { return { s ? s : "" }; }

template<> struct std::formatter<LogPlainW, wchar_t> : std::formatter<std::wstring_view, wchar_t> {
	auto format(const LogPlainW& p, auto& ctx) const { return std::formatter<std::wstring_view, wchar_t>::format(p.s, ctx); }
};
template<> struct std::formatter<LogPlainA, char> : std::formatter<std::string_view, char> {
	auto format(const LogPlainA& p, auto& ctx) const { return std::formatter<std::string_view, char>::format(p.s, ctx); }
};

namespace _log_int {

	// Адрес - в памяти программы только для чтения (код, константы): там строки-литералы ("off", L"a letter", LOC(...)),
	// набранного там не бывает.
	inline bool InReadOnlyImage(const void* p) {
		struct Range { uintptr_t from, to; };
		static const std::vector<Range> ranges = [] {
			std::vector<Range> r;
			const auto base = (const BYTE*)GetModuleHandleW(nullptr);
			const auto nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
			const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
			for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
				if (!(sec->Characteristics & IMAGE_SCN_MEM_WRITE))
					r.push_back({ (uintptr_t)base + sec->VirtualAddress, (uintptr_t)base + sec->VirtualAddress + sec->Misc.VirtualSize });
			return r;
		}();
		const auto a = (uintptr_t)p;
		for (const auto& x : ranges)
			if (a >= x.from && a < x.to) return true;
		return false;
	}

	// Вместо строки - число её символов: "‹6›".
	inline std::wstring MaskW(size_t n) { return std::format(L"‹{}›", n); }
	inline std::string MaskA(std::string_view utf8) {
		size_t n = 0;
		for (unsigned char c : utf8) n += (c & 0xC0) != 0x80;
		return std::format("\xE2\x80\xB9{}\xE2\x80\xBA", n);
	}

	// Значение записи - каким оно идёт в строку журнала (выше: что закрыто в безопасном).
	template<class T> auto Guard(bool hide, T&& v) {
		using D = std::remove_cvref_t<T>;
		using E = std::remove_cv_t<std::remove_pointer_t<std::decay_t<T>>>;
		if constexpr (std::is_same_v<D, LogPlainW> || std::is_same_v<D, LogPlainA>) {
			return D(v);
		}
		else if constexpr (std::is_same_v<D, std::wstring> || std::is_same_v<D, std::wstring_view>) {
			return !hide || InReadOnlyImage(std::wstring_view(v).data()) ? std::wstring(v) : MaskW(std::wstring_view(v).size());
		}
		else if constexpr (std::is_same_v<D, std::string> || std::is_same_v<D, std::string_view>) {
			return !hide || InReadOnlyImage(std::string_view(v).data()) ? std::string(v) : MaskA(v);
		}
		else if constexpr (std::is_pointer_v<std::decay_t<T>> && std::is_same_v<E, wchar_t>) {
			const wchar_t* p = v ? v : L"";
			return !hide || InReadOnlyImage(p) ? std::wstring(p) : MaskW(wcslen(p));
		}
		else if constexpr (std::is_pointer_v<std::decay_t<T>> && std::is_same_v<E, char>) {
			const char* p = v ? v : "";
			return !hide || InReadOnlyImage(p) ? std::string(p) : MaskA(p);
		}
		else if constexpr (std::is_same_v<D, std::filesystem::path>) {
			return !hide ? v.wstring() : MaskW(v.native().size());
		}
		else if constexpr (std::is_same_v<D, wchar_t> || std::is_same_v<D, char>) {
			return hide ? D('*') : D(v);
		}
		else {
			return D(v);
		}
	}

	// Строка журнала собирается в том потоке, который пишет, а в файл её пишет свой поток (SwLogger::Writer): запись на
	// диск иногда ждёт сотни миллисекунд, а журнал пишет и перехват клавиш и мыши - Windows ждёт его на каждое нажатие и
	// движение мыши (тормозят набор и указатель), а не дождавшись, молча отключает перехват.
	class LogLine {
	public:
		std::wstring s;
		void Append(const TChar* data) {
			if (data) s += data;
		}
		void Append(const char* data) { // UTF-8 (setlocale в WinMain - как прежний вывод через %S)
			if (!data || !*data) return;
			const int n = MultiByteToWideChar(CP_UTF8, 0, data, -1, nullptr, 0);
			if (n <= 1) return;
			const size_t at = s.size();
			s.resize(at + n - 1);
			MultiByteToWideChar(CP_UTF8, 0, data, -1, s.data() + at, n);
		}
		void AppendPrefix() {
			SYSTEMTIME st;
			::GetLocalTime(&st);
			s += std::format(L"{:02}.{:02}|{:02}:{:02}:{:02}.{:03}|{:05} ", st.wDay, st.wMonth, st.wHour, st.wMinute,
				st.wSecond, st.wMilliseconds, GetCurrentThreadId());
		}
		// Значения - через Guard: в безопасном журнале строки закрыты (выше).
		template<typename... Args>
		void AppendFormat(const std::wformat_string<Args...>& f, Args&&... v) {
			auto guarded = std::make_tuple(Guard(safe, std::forward<Args>(v))...);
			std::apply([&](auto&... a) { s += std::vformat(f.get(), std::make_wformat_args(a...)); }, guarded);
		}
		template<typename... Args>
		void AppendFormat(const std::format_string<Args...>& f, Args&&... v) {
			auto guarded = std::make_tuple(Guard(safe, std::forward<Args>(v))...);
			std::apply([&](auto&... a) { Append(std::vformat(f.get(), std::make_format_args(a...)).c_str()); }, guarded);
		}
		bool safe = _log_int::safe; // строка собрана в безопасном журнале (SwLogger: в файл отчёта - только такие)
	};

	class SwLogger {
	public:
		// Не уничтожается: поток записи может дописывать и при выходе (atexit ждёт его не дольше секунды).
		static SwLogger& Get() {
			static SwLogger* logger = new SwLogger();
			return *logger;
		}
		// Готовая строка (с \n) - в очередь; держит только очередь, не файл. safe - собрана в безопасном журнале.
		void Push(std::wstring&& line, bool safe) {
			{
				std::lock_guard lock(m_mtx);
				if (m_queue.size() >= 50000) { // диск совсем встал - не копить память
					m_dropped++;
					return;
				}
				m_queue.push_back({ std::move(line), safe });
				if (!m_started) {
					m_started = true;
					std::thread([this] { Writer(); }).detach();
					std::atexit([] { Get().Finish(); });
					// Падение и std::terminate: atexit не зовётся, а строки перед ними - самые нужные. Дописать очередь
					// (не дольше секунды) и падать дальше как обычно. set_terminate в MSVC - только для этого потока;
					// abort из любого потока (terminate в потоке хука, движка) сначала поднимает SIGABRT - он общий.
					std::set_terminate([] {
						Get().Finish(true);
						std::abort();
					});
					std::signal(SIGABRT, [](int) { Get().Finish(true); });
					SetUnhandledExceptionFilter([](EXCEPTION_POINTERS*) -> LONG {
						Get().Finish(true);
						return EXCEPTION_CONTINUE_SEARCH;
					});
				}
			}
			m_cv.notify_one();
		}
		// Файл безопасного журнала (LogSafe) - log\FluentSwitcher-report.log, новый при каждом включении; в него - только
		// строки, собранные в безопасном режиме (обычные, оставшиеся в очереди, - мимо). Выключили - дальше в обычный
		// файл, он дописывается.
		void SetReportFile(bool on) {
			std::lock_guard lock(m_mtx);
			m_report = on;
			m_targetGen++;
		}
		// Дождаться, пока очередь записана (не дольше секунды).
		void Flush() { Finish(); }
		static std::wstring Folder() {
			wchar_t path[MAX_PATH * 2] = {};
			if (!GetModuleFileNameEx(GetCurrentProcess(), GetCurrentModule(), path, (DWORD)std::size(path))) return {};
			if (wchar_t* last = wcsrchr(path, L'\\')) *last = 0;
			return std::wstring(path) + L"\\log";
		}
		static std::wstring ReportPath() { return Folder() + L"\\FluentSwitcher-report.log"; }

	private:
		struct Queued {
			std::wstring line;
			bool safe = false;
		};
		void Writer() {
			std::unique_lock lock(m_mtx);
			while (true) {
				m_cv.wait(lock, [this] { return !m_queue.empty(); });
				std::deque<Queued> batch;
				batch.swap(m_queue);
				const size_t dropped = std::exchange(m_dropped, 0);
				const bool report = m_report;
				const int gen = m_targetGen;
				m_writing = true;
				lock.unlock();
				if (gen != m_openGen) { // файл сменился (SetReportFile)
					if (m_fp) fclose(m_fp);
					m_fp = NULL;
					m_tryOpen = false;
					m_openGen = gen;
					m_openReport = report;
				}
				if (FILE* fp = LazyOpen()) {
					for (const auto& q : batch)
						if (q.safe || !m_openReport) fputws(q.line.c_str(), fp);
					if (dropped) fwprintf_s(fp, L"[log: %zu lines dropped, the disk did not keep up]\n", dropped);
					fflush(fp);
				}
				lock.lock();
				m_writing = false;
				m_idleCv.notify_all();
			}
		}
		// Выход: дописать очередь (не дольше секунды). crash - при падении: очередь мог держать упавший поток - ждать её
		// не дольше 0,1 с (обычно её держат микросекунды: другой поток кладёт строку).
		void Finish(bool crash = false) {
			std::unique_lock lock(m_mtx, std::defer_lock);
			if (!crash)
				lock.lock();
			else {
				for (int i = 0; i < 100 && !lock.try_lock(); i++) Sleep(1);
				if (!lock.owns_lock()) return;
			}
			m_idleCv.wait_for(lock, std::chrono::seconds(1), [this] { return m_queue.empty() && !m_writing; });
		}
		FILE* LazyOpen() {
			if (!m_fp) {
				if (!m_tryOpen) {
					m_tryOpen = true;

					const std::wstring folder = Folder();
					if (folder.empty())
						return NULL;
					CreateDirectory(folder.c_str(), NULL);
					if (m_openReport) {
						m_fp = _wfsopen(ReportPath().c_str(), L"wt, ccs=UTF-8", _SH_DENYNO);
						return m_fp;
					}
					TChar base[512];
					base[0] = 0;
					GetModuleBaseName(GetCurrentProcess(), NULL, base, std::ssize(base));

					auto path = std::format(L"{}\\{}.log", folder, base);
					// Первый раз за запуск - заново; после безопасного журнала - дописать.
					m_fp = _wfsopen(path.c_str(), m_mainOpened ? L"at, ccs=UTF-8" : L"wt, ccs=UTF-8", _SH_DENYNO);
					m_mainOpened = m_mainOpened || m_fp;
				}
			}
			return m_fp;
		}
		std::mutex m_mtx; // очередь
		std::condition_variable m_cv, m_idleCv;
		std::deque<Queued> m_queue;
		size_t m_dropped = 0;
		bool m_started = false, m_writing = false;
		bool m_report = false; // под m_mtx: файл безопасного журнала (SetReportFile)
		int m_targetGen = 0;
		FILE* m_fp = NULL; // только поток записи
		bool m_tryOpen = false;
		bool m_openReport = false, m_mainOpened = false; // только поток записи
		int m_openGen = 0;
	};

	inline SwLogger& SwLoggerGlobal() { return SwLogger::Get(); }

	template<bool iswarn = false> void __LOG_LINE_FORMAT(auto&&... v) {
		LogLine line;
		line.AppendPrefix();
		if constexpr (iswarn) line.Append("[WARN] ");
		line.AppendFormat(FORWARD(v)...);
		line.s += L'\n';
		SwLoggerGlobal().Push(std::move(line.s), line.safe);
	}

	class WinErrBOOL {
		bool m_res = false;
		DWORD m_dwErr = 0;
	public:
		WinErrBOOL(BOOL r) : m_res(r) {}
		void Log(LogLine& line) const {
			line.AppendFormat(L"WinErr={} ", m_dwErr);

			CAutoWinMem lpMsgBuf;
			FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				m_dwErr,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPTSTR)&lpMsgBuf,
				0, NULL);
			line.Append((TStr)lpMsgBuf.get());
		}
		operator bool() {
			if (m_res)
				return false;
			m_dwErr = GetLastError();
			return true;
		}
		TStatus ToTStatus() { return SW_ERR_WINAPI; }
	};

	struct SwErrTStatus {
		TStatus res;
		SwErrTStatus(TStatus r) : res(r) {}
		void Log(LogLine& line) const {
			line.AppendFormat("TStatus={}({})", (int)res, simple_enum::enum_name(res));
		}
		operator bool() const { return res != SW_ERR_SUCCESS; }
		TStatus ToTStatus() { return res; }
	};

	struct WinErrLSTATUS {
		LSTATUS res;
		WinErrLSTATUS(LSTATUS r) : res(r) {}
		void Log(LogLine& line) const { line.AppendFormat(L"LSTATUS={}", (int)res); }
		bool IsError() const { return res != ERROR_SUCCESS; }
		operator bool() const { return IsError(); }
		TStatus ToTStatus() { return SW_ERR_WINAPI; }
	};

	struct WinErrHRESULT {
		HRESULT res;
		WinErrHRESULT(HRESULT r) : res(r) {}
		void Log(LogLine& line) const { line.AppendFormat(L"HResult={}(0x{:x})", res, res); }
		operator bool() const { return FAILED(res); }
		TStatus ToTStatus() { return SW_ERR_HRESULT; }
	};


	template<typename... Args>
	inline void __Log_Err_Common(const auto& err, std::source_location loc, const std::wformat_string<Args...> s, Args&&... v) {
		if (GetLogLevel() < LOG_LEVEL_1)
			return;

		LogLine line;
		line.AppendPrefix();
		err.Log(line);
		auto file = loc.file_name();
		auto cur = strrchr(file, '\\');
		line.AppendFormat("file={}({})", cur ? cur + 1 : file, loc.line());
		line.AppendFormat(s, FORWARD(v)...);
		line.s += L'\n';
		SwLoggerGlobal().Push(std::move(line.s), line.safe);
	}

	inline void __Log_Err_Common(const auto& err, std::source_location loc) { __Log_Err_Common(err, loc, L""); }

	template<typename... Args>
	inline void LOG_ANY_CMN(const std::wformat_string<Args...> s, Args&&... v) { _log_int::__LOG_LINE_FORMAT(s, FORWARD(v)...); }

	template<typename... Args>
	inline void LOG_ANY_CMN(const std::format_string<Args...> s, Args&&... v) { _log_int::__LOG_LINE_FORMAT(s, FORWARD(v)...); }

	template<typename... Args>
	inline void LOG_WARN(const std::wformat_string<Args...> s, Args&&... v) { _log_int::__LOG_LINE_FORMAT<true>(s, FORWARD(v)...); }

	template<typename... Args>
	inline void LOG_WARN(const std::format_string<Args...> s, Args&&... v) { _log_int::__LOG_LINE_FORMAT<true>(s, FORWARD(v)...); }

}

#define  _SW_ERR_RET(ClassName, X, ...) {if (ClassName __res = (X)) { __Log_Err_Common(__res, std::source_location::current(), __VA_ARGS__); return __res.ToTStatus(); } }
#define  _SW_ERR_LOG(ClassName, X, ...) {if (ClassName __res = (X))   __Log_Err_Common(__res, std::source_location::current(), __VA_ARGS__); }

#define IFW_RET(X, ...) _SW_ERR_RET(_log_int::WinErrBOOL, X, __VA_ARGS__)
#define IFW_LOG(X, ...) _SW_ERR_LOG(_log_int::WinErrBOOL, X, __VA_ARGS__)

#define IFS_RET(X, ...) _SW_ERR_RET(_log_int::SwErrTStatus, X, __VA_ARGS__)
#define IFS_LOG(X, ...) _SW_ERR_LOG(_log_int::SwErrTStatus, X, __VA_ARGS__)

#define IF_LSTATUS_RET(X, ...) _SW_ERR_RET(_log_int::WinErrLSTATUS, X, __VA_ARGS__)
#define IF_LSTATUS_LOG(X, ...) _SW_ERR_LOG(_log_int::WinErrLSTATUS, X, __VA_ARGS__)

#define IFH_RET(X, ...) _SW_ERR_RET(_log_int::WinErrHRESULT, X, __VA_ARGS__)
#define IFH_LOG(X, ...) _SW_ERR_LOG(_log_int::WinErrHRESULT, X, __VA_ARGS__)

// Безопасный журнал (LogSafe) - вкл./выкл.: свой файл, свой уровень (3: строки LOG_ANY_4 - посимвольные - не пишутся).
// Выключение ждёт, пока его строки записаны, - файл можно читать сразу.
inline void SetLogSafe(bool on) {
	if (on) {
		SetLogLevel(LOG_LEVEL_DISABLE);
		_log_int::SwLoggerGlobal().SetReportFile(true);
		_log_int::safe = true;
		SetLogLevel(LOG_LEVEL_3);
	}
	else {
		SetLogLevel(LOG_LEVEL_DISABLE);
		_log_int::safe = false;
		_log_int::SwLoggerGlobal().Flush();
		_log_int::SwLoggerGlobal().SetReportFile(false);
	}
}

#define LOG_ANY(...) if (GetLogLevel() >= LOG_LEVEL_2) {_log_int::LOG_ANY_CMN(__VA_ARGS__);}
#define LOG_ANY_4(...) if (GetLogLevel() >= LOG_LEVEL_4) [[unlikely]] {_log_int::LOG_ANY_CMN(__VA_ARGS__);}
#define LOG_WARN(...) if (GetLogLevel() >= LOG_LEVEL_2) {_log_int::LOG_WARN(__VA_ARGS__);}


#define RETURN_SUCCESS {return SW_ERR_SUCCESS; }
