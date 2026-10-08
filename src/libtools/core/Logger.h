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

namespace _log_int {

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
		template<typename... Args>
		void AppendFormat(const std::wformat_string<Args...>& f, Args&&... v) {
			s += std::vformat(f.get(), std::make_wformat_args(v...));
		}
		template<typename... Args>
		void AppendFormat(const std::format_string<Args...>& f, Args&&... v) {
			Append(std::vformat(f.get(), std::make_format_args(v...)).c_str());
		}
	};

	class SwLogger {
	public:
		// Не уничтожается: поток записи может дописывать и при выходе (atexit ждёт его не дольше секунды).
		static SwLogger& Get() {
			static SwLogger* logger = new SwLogger();
			return *logger;
		}
		// Готовая строка (с \n) - в очередь; держит только очередь, не файл.
		void Push(std::wstring&& line) {
			{
				std::lock_guard lock(m_mtx);
				if (m_queue.size() >= 50000) { // диск совсем встал - не копить память
					m_dropped++;
					return;
				}
				m_queue.push_back(std::move(line));
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

	private:
		void Writer() {
			std::unique_lock lock(m_mtx);
			while (true) {
				m_cv.wait(lock, [this] { return !m_queue.empty(); });
				std::deque<std::wstring> batch;
				batch.swap(m_queue);
				const size_t dropped = std::exchange(m_dropped, 0);
				m_writing = true;
				lock.unlock();
				if (FILE* fp = LazyOpen()) {
					for (const auto& line : batch) fputws(line.c_str(), fp);
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

					static const size_t nSize = 0x1000;
					std::unique_ptr<TChar[]> buf(new TChar[nSize]);
					TChar* sFolder = buf.get();
					if (!sFolder) {
						return m_fp;
					}

					if (!GetModuleFileNameEx(GetCurrentProcess(), GetCurrentModule(), sFolder, nSize))
						return NULL;
					TChar* sLast = wcsrchr(sFolder, L'\\');
					if (sLast)
						*sLast = 0;
					wcscat_s(sFolder, nSize, L"\\log");
					CreateDirectory(sFolder, NULL);
					TChar base[512];
					base[0] = 0;
					GetModuleBaseName(GetCurrentProcess(), NULL, base, std::ssize(base));

					auto path = std::format(L"{}\\{}.log", sFolder, base);
					m_fp = _wfsopen(path.c_str(), L"wt, ccs=UTF-8", _SH_DENYNO);
				}
			}
			return m_fp;
		}
		std::mutex m_mtx; // очередь
		std::condition_variable m_cv, m_idleCv;
		std::deque<std::wstring> m_queue;
		size_t m_dropped = 0;
		bool m_started = false, m_writing = false;
		FILE* m_fp = NULL; // только поток записи
		bool m_tryOpen = false;
	};

	inline SwLogger& SwLoggerGlobal() { return SwLogger::Get(); }

	template<bool iswarn = false> void __LOG_LINE_FORMAT(auto&&... v) {
		LogLine line;
		line.AppendPrefix();
		if constexpr (iswarn) line.Append("[WARN] ");
		line.AppendFormat(FORWARD(v)...);
		line.s += L'\n';
		SwLoggerGlobal().Push(std::move(line.s));
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
		SwLoggerGlobal().Push(std::move(line.s));
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

#define LOG_ANY(...) if (GetLogLevel() >= LOG_LEVEL_2) {_log_int::LOG_ANY_CMN(__VA_ARGS__);}
#define LOG_ANY_4(...) if (GetLogLevel() >= LOG_LEVEL_4) [[unlikely]] {_log_int::LOG_ANY_CMN(__VA_ARGS__);}
#define LOG_WARN(...) if (GetLogLevel() >= LOG_LEVEL_2) {_log_int::LOG_WARN(__VA_ARGS__);}


#define RETURN_SUCCESS {return SW_ERR_SUCCESS; }
