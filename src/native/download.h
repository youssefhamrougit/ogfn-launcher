// download.h — in-app downloader for the single 4.10 build (Library flow).
//
// Downloads the build ZIP over HTTP(S) (WinHTTP), reports progress events to
// the UI through bridge events, supports pause / resume / cancel, and after a
// finished download automatically installs the build (extract → import →
// validate → registered).
#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace download {

// Where the build ZIP comes from. PLACEHOLDER until the archive.org item is
// finalized — swap this for the real Internet Archive download URL.
constexpr const char* kBuildUrl =
    "https://archive.org/download/Fortnite-4.10-CL-4053532/4.10-CL-4053532.zip";

// State machine surfaced to the UI.
enum class State {
    Idle,       // nothing downloaded / nothing running
    Downloading,
    Paused,
    Installing, // extracting + importing + validating
    Done,       // build registered and validated
    Error,
};

// The file that holds the in-progress (or finished) download.
// %APPDATA%\OGFNLauncher\downloads\4.10-CL-4053532.zip
std::wstring ZipPathUtf16();
std::string ZipPathUtf8();

// Full status snapshot for the UI (state, bytes, url, error...).
nlohmann::json StatusJson();

// Start (or resume) the download in a background thread.
// Fails with a string when a download is already running or the URL is bad.
bool Start(std::string& err);
void Pause();    // graceful: flush + close, state becomes Paused
void Resume();   // same as Start when paused
void Cancel();   // abort + delete partial file, state becomes Idle

// Re-run install only (extract → import → validate) using the downloaded zip.
bool Install(std::string& err);

// True when a fully-downloaded zip exists but was never installed (e.g. the
// app closed between download and install). Used at startup.
bool HasCompletedZip();

// App is quitting: stop the worker (partial file is kept so the download can
// resume next launch) and join the thread. Call once before CoUninitialize.
void Shutdown();

// Reset to Idle without touching files (used after logs.clearData).
void ResetToIdle();

} // namespace download
