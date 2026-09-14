// log.cpp — simple append-only log with timestamp + level.
#include "log.h"
#include "util.h"

#include <windows.h>

#include <cstdio>
#include <mutex>

namespace ogfnlog {

static std::mutex g_mutex;
static bool g_initialized = false;

static const char* LevelName(Level l) {
    switch (l) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?????";
}

void Init() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        util::EnsureDir(util::LogDir());

        // Rotate: launcher.log -> launcher.1.log (single generation is enough).
        std::wstring path = util::LogFilePath();
        std::wstring prev = util::LogDir() + L"\\launcher.1.log";
        DeleteFileW(prev.c_str());
        MoveFileW(path.c_str(), prev.c_str());

        util::WriteTextFile(path, "");
        g_initialized = true;
    }
    // Log outside the lock — Write() takes the same (non-recursive) mutex.
    Info("OGFN Launcher " OGFN_VERSION " starting");
}

void Write(Level level, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    char line[1024];
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
                        "[%04hu-%02hu-%02hu %02hu:%02hu:%02hu.%03hu] %s %s\r\n",
                        st.wYear, st.wMonth, st.wDay,
                        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                        LevelName(level), message.c_str());
    if (n <= 0) return;

    HANDLE h = CreateFileW(util::LogFilePath().c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, line, (DWORD)n, &written, nullptr);
    CloseHandle(h);
}

void Debug(const std::string& msg) { Write(Level::Debug, msg); }
void Info(const std::string& msg)  { Write(Level::Info,  msg); }
void Warn(const std::string& msg)  { Write(Level::Warn,  msg); }
void Error(const std::string& msg) { Write(Level::Error, msg); }

} // namespace ogfnlog
