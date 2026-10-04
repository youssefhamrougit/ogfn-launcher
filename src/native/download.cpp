// download.cpp — WinHTTP downloader + auto-install orchestration.
//
// One background worker does: download (resumable) → extract (Shell COM) →
// import → validate. The UI polls via download.status and also receives
// throttled event pushes through the bridge.
#include "download.h"

#include "bridge.h"
#include "build410.h"
#include "config.h"
#include "log.h"
#include "util.h"

#include <windows.h>
#include <objbase.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp")

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <memory>
#include <mutex>
#include <thread>

namespace download {

using nlohmann::json;

namespace {

// ---------------------------------------------------------------- state

struct Shared {
    std::mutex mu;
    std::string state = "idle"; // idle|downloading|paused|installing|done|error
    std::string url = kBuildUrl;
    std::string error;
    unsigned long long bytesNow = 0;
    unsigned long long bytesTotal = 0;
    std::string phase;         // installing: "extracting" | "importing" | "validating"
    double phaseProgress = 0;  // installing: 0..1 within phase
    std::string zipPathUtf8;   // filled once the download file exists
};

Shared g;

std::thread g_thread;
std::atomic<bool> g_workerAlive{false};
std::atomic<int> g_command{0}; // 0 = run/resume, 1 = pause, 2 = cancel/stop

constexpr const wchar_t* kZipName = L"4.10-CL-4053532.zip";

std::wstring DownloadsDir() {
    std::wstring d = util::AppDataDir() + L"\\downloads";
    util::EnsureDir(d);
    return d;
}

std::wstring ZipPathUtf16Impl() { return DownloadsDir() + L"\\" + kZipName; }

unsigned long long FileSizeOf(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa)) return 0;
    return (static_cast<unsigned long long>(fa.nFileSizeHigh) << 32) |
           fa.nFileSizeLow;
}

// Marker next to the zip meaning "fully downloaded, not yet installed".
std::wstring CompletedFlagPath() { return DownloadsDir() + L"\\completed.flag"; }
void MarkZipCompleted() { util::WriteTextFile(CompletedFlagPath(), "ok"); }
void ClearZipCompleted() { util::DeleteFileSilent(CompletedFlagPath()); }

// Status snapshot. Caller must NOT hold g.mu.
json StatusSnapshot() {
    std::lock_guard<std::mutex> lk(g.mu);
    json j{
        {"state", g.state},
        {"url", g.url},
        {"error", g.error},
        {"bytesNow", g.bytesNow},
        {"bytesTotal", g.bytesTotal},
        {"zipPath", g.zipPathUtf8},
        {"fileName", util::ToUtf8(kZipName)},
    };
    if (!g.phase.empty()) {
        j["phase"] = g.phase;
        j["phaseProgress"] = g.phaseProgress;
    }
    return j;
}

std::string GetState() {
    std::lock_guard<std::mutex> lk(g.mu);
    return g.state;
}

// ------------------------------------------------------------ ui events

namespace ev {

// Throttle progress pushes (the UI also polls; events just smooth the bar).
using Clock = std::chrono::steady_clock;
Clock::time_point g_lastPush = Clock::now() - std::chrono::seconds(1);
unsigned long long g_lastPushedBytes = 0;

void ProgressNow(unsigned long long now, unsigned long long total, int rateKb) {
    auto sinceMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                       Clock::now() - g_lastPush).count();
    bool bigJump = now > g_lastPushedBytes &&
                   now - g_lastPushedBytes >= 8 * 1024 * 1024;
    if (sinceMs < 500 && !bigJump) return;
    g_lastPush = Clock::now();
    g_lastPushedBytes = now;

    json d{{"state", GetState()},
           {"bytesNow", now},
           {"bytesTotal", total},
           {"rate", rateKb}};
    if (total > 0) d["percent"] = (double)now * 100.0 / (double)total;
    bridge::PostEvent("download.progress", d);
}

void StateChanged() { bridge::PostEvent("download.state", StatusSnapshot()); }

void Error(std::string msg) {
    json d = StatusSnapshot();
    d["error"] = msg;
    bridge::PostEvent("download.error", d);
}

} // namespace ev

// ------------------------------------------------------------ http piece

// Content-Length header value (case-insensitive scan), 0 when absent.
unsigned long long ParseContentLength(const wchar_t* headers) {
    std::wstring low(headers);
    for (auto& c : low) c = (wchar_t)towlower(c);
    size_t p = low.find(L"content-length:");
    if (p == std::wstring::npos) return 0;
    return wcstoull(headers + p + 15, nullptr, 10);
}

// RunRanges: one HTTP GET with "Range: bytes=<from>-", appending to the zip.
// Returns:
//   0  connection ended (caller checks total vs file size; may resume)
//   1  pause requested
//   2  cancel requested
//   3  error (err set)
//   4  HTTP 416 — file already fully downloaded
// totalFromServer: full file size as reported by the server (0 if unknown).
int RunRange(HINTERNET session, const std::wstring& host, INTERNET_PORT port,
             const std::wstring& path, bool useTls, unsigned long long from,
             const std::atomic<bool>& stopFlag, std::string& err,
             unsigned long long& totalFromServer) {
    totalFromServer = 0;

    HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
    if (!connect) {
        err = "Could not connect to the download server (error " +
              std::to_string(GetLastError()) + ").";
        return 3;
    }

    int rc = 3;
    HINTERNET request = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;

    std::wstring range =
        L"Range: bytes=" + std::to_wstring(from) + L"-";
    DWORD flags = useTls ? WINHTTP_FLAG_SECURE : 0;
    request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 flags);
    if (!request) {
        err = "Could not create the HTTP request.";
        goto done;
    }
    if (!WinHttpAddRequestHeaders(request, range.c_str(), (DWORD)-1,
                                  WINHTTP_ADDREQ_FLAG_ADD)) {
        err = "Could not prepare the range request.";
        goto done;
    }

    // Slow archive hosts need generous connect time; a 30s silent read cuts
    // dead connections so the outer loop can resume.
    {
        DWORD t = 45000;
        WinHttpSetOption(request, WINHTTP_OPTION_CONNECT_TIMEOUT, &t, sizeof(t));
        t = 30000;
        WinHttpSetOption(request, WINHTTP_OPTION_SEND_TIMEOUT, &t, sizeof(t));
        t = 30000;
        WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, &t, sizeof(t));
    }

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        err = "The download server did not respond (error " +
              std::to_string(GetLastError()) + ").";
        goto done;
    }

    {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
            WINHTTP_NO_HEADER_INDEX);

        wchar_t hdrs[16384] = {};
        DWORD hdrsLen = sizeof(hdrs) - sizeof(wchar_t);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                WINHTTP_HEADER_NAME_BY_INDEX, hdrs, &hdrsLen,
                                WINHTTP_NO_HEADER_INDEX)) {
            unsigned long long cl = ParseContentLength(hdrs);
            if (cl > 0) {
                // 206 → cl is the remaining bytes; 200 → whole file.
                totalFromServer = (status == 206) ? from + cl : cl;
            }
        }

        if (status == 416) {
            // Range not satisfiable → we already have the whole file.
            rc = 4;
            goto done;
        }
        if (status != 206 && status != 200) {
            err = "Download server replied HTTP " + std::to_string(status) +
                  (status == 404
                       ? " (file not found — the download link may not be live "
                         "yet; the launcher currently uses a placeholder URL)."
                       : ".");
            goto done;
        }

        if (status == 200 && from > 0) {
            // Server ignored the Range header. Restarting automatically would
            // surprise the user; surface it instead.
            err = "The server does not support resuming. Cancel and start the "
                  "download again to restart from the beginning.";
            goto done;
        }

        DWORD access = (status == 200) ? GENERIC_WRITE : FILE_APPEND_DATA;
        DWORD creation = (status == 200) ? CREATE_ALWAYS : OPEN_EXISTING;
        file = CreateFileW(ZipPathUtf16Impl().c_str(), access, 0, nullptr,
                           creation, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            err = "Could not open the download file for writing. Check free "
                  "disk space (~28 GB needed).";
            goto done;
        }
        if (status == 200) {
            std::lock_guard<std::mutex> lk(g.mu);
            g.bytesNow = 0;
        }
    }

    // Read → append loop.
    {
        static const DWORD kBufSize = 512 * 1024;
        std::unique_ptr<char[]> buf(new char[kBufSize]);
        DWORD lastTick = GetTickCount();
        unsigned long long lastBytes = 0;
        int lastRate = 0;

        for (;;) {
            if (g_command.load() != 0 || stopFlag.load()) {
                rc = (g_command.load() == 2) ? 2 : 1;
                goto done;
            }

            DWORD got = 0;
            if (!WinHttpReadData(request, buf.get(), kBufSize, &got)) {
                err = "Connection lost during download (error " +
                      std::to_string(GetLastError()) + ").";
                goto done;
            }
            if (got == 0) break; // server closed — caller decides via total

            DWORD put = 0;
            if (!WriteFile(file, buf.get(), got, &put, nullptr) || put != got) {
                err = "Disk write failed — free up space (about 28 GB is "
                      "needed) and retry.";
                goto done;
            }
            {
                std::lock_guard<std::mutex> lk(g.mu);
                g.bytesNow += got;
            }

            DWORD now = GetTickCount();
            if (now - lastTick >= 1000) {
                unsigned long long nowBytes;
                {
                    std::lock_guard<std::mutex> lk(g.mu);
                    nowBytes = g.bytesNow;
                    lastRate = (int)((nowBytes - lastBytes) /
                                     ((now - lastTick) / 1000.0) / 1024);
                }
                lastBytes = nowBytes;
                lastTick = now;
                unsigned long long total;
                {
                    std::lock_guard<std::mutex> lk(g.mu);
                    total = g.bytesTotal;
                }
                ev::ProgressNow(nowBytes, total, lastRate);
            }
        }
        FlushFileBuffers(file);
        rc = 0;
    }

done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (request) WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    return rc;
}

// ------------------------------------------------------------ worker

enum class InstallOutcome { Ok, Failed, Cancelled };

// Extract → import → validate. Updates shared phase + progress.
InstallOutcome DoInstall(const std::shared_ptr<std::atomic<bool>>& stopFlag) {
    auto cancelled = [&stopFlag]() {
        return stopFlag->load() || g_command.load() == 2;
    };
    auto setPhase = [&](const char* phase) {
        {
            std::lock_guard<std::mutex> lk(g.mu);
            g.phase = phase;
            g.phaseProgress = 0;
        }
        ev::StateChanged();
    };

    setPhase("extracting");

    // The extracted build is ~28 GB, so it goes next to nothing in %APPDATA%:
    // default destination is <SystemDrive>\OGFN Builds\4.10-CL-4053532.
    wchar_t drive[4] = L"C:\\";
    {
        wchar_t sd[4] = {};
        if (GetEnvironmentVariableW(L"SystemDrive", sd, 4) > 0 && sd[1] == L':')
            memcpy(drive, sd, sizeof(sd));
    }
    std::wstring extractParent = std::wstring(drive) + L"OGFN Builds";

    ogfnlog::Info("install: extracting to " + util::ToUtf8(extractParent));

    std::string err;
    auto onProgress = [&](double p) {
        bool push = false;
        {
            std::lock_guard<std::mutex> lk(g.mu);
            if (p - g.phaseProgress > 0.005 || p >= 1.0) {
                g.phaseProgress = p;
                push = true;
            }
        }
        if (push) ev::StateChanged();
    };

    if (!util::ExtractZipShell(ZipPathUtf16Impl(), extractParent, cancelled,
                               onProgress, err)) {
        if (cancelled()) return InstallOutcome::Cancelled;
        ogfnlog::Error("install: extract failed: " + err);
        {
            std::lock_guard<std::mutex> lk(g.mu);
            g.error = "Extraction failed: " + err;
        }
        return InstallOutcome::Failed;
    }

    setPhase("importing");
    std::wstring root = build410::LocateBuildRoot(extractParent);
    if (root.empty()) {
        std::lock_guard<std::mutex> lk(g.mu);
        g.error = "Extracted build is missing FortniteClient-Win64-Shipping.exe "
                  "— the archive may be incomplete.";
        return InstallOutcome::Failed;
    }

    auto imp = build410::ImportFolder(root);
    if (!imp.ok) {
        std::lock_guard<std::mutex> lk(g.mu);
        g.error = "Import failed: " + imp.error;
        return InstallOutcome::Failed;
    }
    ogfnlog::Info("install: imported build at " + util::ToUtf8(root));

    setPhase("validating");
    auto val = build410::ValidateRegistered();
    if (!val.ok) {
        std::lock_guard<std::mutex> lk(g.mu);
        g.error = "Validation failed: " + val.error;
        return InstallOutcome::Failed;
    }

    {
        std::lock_guard<std::mutex> lk(g.mu);
        g.phase.clear();
        g.phaseProgress = 0;
    }
    return InstallOutcome::Ok;
}

void WorkerMain(std::shared_ptr<std::atomic<bool>> stopFlag, bool installOnly) {
    HRESULT cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)cohr; // extraction falls back gracefully without COM init

    auto finishOk = [&]() {
        {
            std::lock_guard<std::mutex> lk(g.mu);
            g.state = "done";
            g.error.clear();
            g.phase.clear();
            g.phaseProgress = 0;
        }
        ev::StateChanged();
    };
    auto finishError = [&]() {
        std::string msg;
        {
            std::lock_guard<std::mutex> lk(g.mu);
            g.state = "error";
            msg = g.error;
        }
        ev::Error(msg);
    };
    auto resetIdle = [&]() {
        std::lock_guard<std::mutex> lk(g.mu);
        g.state = "idle";
        g.error.clear();
        g.bytesNow = 0;
        g.bytesTotal = 0;
        g.phase.clear();
        g.phaseProgress = 0;
    };

    bool restart = true;
    while (restart && !stopFlag->load()) {
        restart = false;

        // ---------- install-only mode (download already complete) ----------
        if (installOnly) {
            InstallOutcome out = DoInstall(stopFlag);
            if (out == InstallOutcome::Ok) {
                finishOk();
                ClearZipCompleted();
            } else if (out == InstallOutcome::Cancelled) resetIdle();
            else finishError();
            break;
        }

        // ---------- download ----------
        {
            std::lock_guard<std::mutex> lk(g.mu);
            g.state = "downloading";
            g.error.clear();
            g.zipPathUtf8 = util::ToUtf8(ZipPathUtf16Impl());
        }
        ev::StateChanged();

        // Parse the URL once per session.
        std::wstring wurl;
        {
            std::lock_guard<std::mutex> lk(g.mu);
            wurl = util::ToUtf16(g.url);
        }
        URL_COMPONENTSW uc{};
        uc.dwStructSize = sizeof(uc);
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
            std::lock_guard<std::mutex> lk(g.mu);
            g.error = "The download URL is invalid.";
            finishError();
            break;
        }
        std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
        std::wstring path = (uc.lpszUrlPath && uc.dwUrlPathLength)
                                ? std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength)
                                : L"/";
        bool useTls = (uc.nScheme == INTERNET_SCHEME_HTTPS);
        INTERNET_PORT port = uc.nPort;

        HINTERNET session = WinHttpOpen(L"OGFNLauncher/0.1",
                                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            std::lock_guard<std::mutex> lk(g.mu);
            g.error = "Could not initialize the network layer.";
            finishError();
            break;
        }

        bool needInstall = false;
        bool paused = false;
        bool cancelledNow = false;
        int backoffSeconds = 3;

        while (!stopFlag->load()) {
            // Cancel observed between ranges?
            if (g_command.load() == 2) {
                cancelledNow = true;
                break;
            }

            std::wstring zip = ZipPathUtf16Impl();
            unsigned long long have = FileSizeOf(zip);
            {
                std::lock_guard<std::mutex> lk(g.mu);
                g.bytesNow = have;
            }

            unsigned long long totalFromServer = 0;
            std::string rangeErr;
            int rc = RunRange(session, host, port, path, useTls, have, *stopFlag,
                              rangeErr, totalFromServer);

            if (totalFromServer > 0) {
                std::lock_guard<std::mutex> lk(g.mu);
                g.bytesTotal = totalFromServer;
            }

            if (rc == 4) { // 416 → file already complete
                MarkZipCompleted();
                needInstall = true;
                break;
            }
            if (rc == 0) {
                unsigned long long total, sizeNow;
                {
                    std::lock_guard<std::mutex> lk(g.mu);
                    total = g.bytesTotal;
                }
                sizeNow = FileSizeOf(zip);
                if (total == 0 || sizeNow >= total) {
                    MarkZipCompleted();
                    needInstall = true;
                    break;
                }
                backoffSeconds = 3; // connection dropped mid-file — resume now
                continue;
            }
            if (rc == 1) { // pause
                paused = true;
                break;
            }
            if (rc == 2) { // cancel
                cancelledNow = true;
                break;
            }

            // rc == 3 → error: show it, then keep retrying with backoff.
            {
                std::lock_guard<std::mutex> lk(g.mu);
                g.error = rangeErr;
            }
            ev::Error(rangeErr);
            ogfnlog::Warn("download: " + rangeErr);

            bool retry = true;
            for (int s = 0; s < backoffSeconds * 10; ++s) {
                Sleep(100);
                int cmd = g_command.load();
                if (cmd == 2 || stopFlag->load()) { retry = false; break; }
                if (cmd == 1) break; // pause mid-backoff
            }
            int cmd = g_command.load();
            if (!retry || cmd == 2 || stopFlag->load()) {
                if (cmd == 2) cancelledNow = true;
                break;
            }
            if (cmd == 1) {
                paused = true;
                break;
            }
            backoffSeconds = backoffSeconds * 2 < 60 ? backoffSeconds * 2 : 60;
            // loop → range-resume from file size
        }

        WinHttpCloseHandle(session);

        if (cancelledNow) {
            DeleteFileW(ZipPathUtf16Impl().c_str());
            ClearZipCompleted();
            resetIdle();
            ev::StateChanged();
            break;
        }
        if (paused) {
            // Wait until resumed (command 0) / cancelled (2) / shutdown.
            while (!stopFlag->load()) {
                int cmd = g_command.load();
                if (cmd == 0) break;
                if (cmd == 2) {
                    DeleteFileW(ZipPathUtf16Impl().c_str());
                    ClearZipCompleted();
                    resetIdle();
                    ev::StateChanged();
                    return;
                }
                Sleep(150);
            }
            if (stopFlag->load()) break;
            restart = true; // resume the download loop from file size
            continue;
        }
        if (needInstall) {
            InstallOutcome out = DoInstall(stopFlag);
            if (out == InstallOutcome::Ok) {
                finishOk();
                ClearZipCompleted();
                ogfnlog::Info("install: build ready");
            } else if (out == InstallOutcome::Cancelled) {
                DeleteFileW(ZipPathUtf16Impl().c_str());
                ClearZipCompleted();
                resetIdle();
                ev::StateChanged();
            } else {
                finishError();
            }
            break;
        }
        break; // shutdown
    }

    if (SUCCEEDED(cohr)) CoUninitialize();
    g_workerAlive.store(false);
}

bool SpawnWorker(bool installOnly) {
    auto stop = std::make_shared<std::atomic<bool>>(false);
    try {
        std::thread t(WorkerMain, stop, installOnly);
        t.detach(); // lifetime controlled by g_command + self-owned handles
        g_workerAlive.store(true);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

// ---------------------------------------------------------------- API

std::wstring ZipPathUtf16() { return ZipPathUtf16Impl(); }
std::string ZipPathUtf8() { return util::ToUtf8(ZipPathUtf16Impl()); }

json StatusJson() { return StatusSnapshot(); }

bool HasCompletedZip() {
    // A previous download completed when a marker file sits next to the zip.
    // Cheap and survives restarts without parsing state from file sizes.
    return GetFileAttributesW((DownloadsDir() + L"\\completed.flag").c_str()) !=
           INVALID_FILE_ATTRIBUTES;
}

bool Start(std::string& err) {
    std::string st = GetState();
    if (st == "downloading" || st == "installing") {
        err = "A download is already running.";
        return false;
    }
    if (st == "paused" && g_workerAlive.load()) {
        // Worker is waiting — just resume it.
        g_command.store(0);
        std::lock_guard<std::mutex> lk(g.mu);
        g.state = "downloading";
        ev::StateChanged();
        return true;
    }
    if (g_workerAlive.load()) {
        err = "The download worker is still stopping. Try again in a moment.";
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(g.mu);
        g.error.clear();
        if (g.url.empty()) g.url = kBuildUrl;
    }
    g_command.store(0);
    if (!SpawnWorker(false)) {
        err = "Could not start the download worker.";
        return false;
    }
    return true;
}

void Pause() {
    if (GetState() != "downloading") return;
    g_command.store(1);
    {
        std::lock_guard<std::mutex> lk(g.mu);
        g.state = "paused";
    }
    ev::StateChanged();
}

void Resume() {
    if (GetState() != "paused") return;
    g_command.store(0);
    {
        std::lock_guard<std::mutex> lk(g.mu);
        g.state = "downloading";
    }
    ev::StateChanged();
}

void Cancel() {
    std::string st = GetState();
    if (st != "downloading" && st != "paused" && st != "installing") return;
    g_command.store(2);
    // Worker notices within ~a second; wait for it to wind down (max ~8s so a
    // stuck WinHTTP read doesn't hang the UI thread forever).
    for (int i = 0; i < 80 && g_workerAlive.load(); ++i) Sleep(100);
    DeleteFileW(ZipPathUtf16Impl().c_str());
    ClearZipCompleted();
    {
        std::lock_guard<std::mutex> lk(g.mu);
        g.state = "idle";
        g.error.clear();
        g.bytesNow = 0;
        g.bytesTotal = 0;
        g.phase.clear();
        g.phaseProgress = 0;
        g.zipPathUtf8.clear();
    }
    ev::StateChanged();
}

bool Install(std::string& err) {
    std::string st = GetState();
    if (st == "downloading" || st == "installing") {
        err = "A download is already running.";
        return false;
    }
    if (g_workerAlive.load()) {
        err = "The download worker is still stopping. Try again in a moment.";
        return false;
    }
    if (FileSizeOf(ZipPathUtf16Impl()) == 0) {
        err = "No downloaded build file found.";
        return false;
    }
    g_command.store(0);
    if (!SpawnWorker(true)) {
        err = "Could not start the install worker.";
        return false;
    }
    return true;
}

void Shutdown() {
    g_command.store(2);
    // Worker is detached; give it a short, bounded window to notice. It never
    // outlives the process because it only touches self-owned handles.
    for (int i = 0; i < 50 && g_workerAlive.load(); ++i) Sleep(100);
}

void ResetToIdle() {
    std::lock_guard<std::mutex> lk(g.mu);
    g.state = "idle";
    g.error.clear();
    g.bytesNow = 0;
    g.bytesTotal = 0;
    g.phase.clear();
    g.phaseProgress = 0;
    g.zipPathUtf8.clear();
}

} // namespace download
