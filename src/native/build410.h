// build410.h — Fortnite 4.10 (CL 4053532) build import + validation.
#pragma once

#include <string>

namespace build410 {

// The one build this launcher manages.
constexpr const char* kBuildId = "4.10-CL-4053532";

struct ImportResult {
    bool ok = false;
    std::string gameExePath;      // absolute path to FortniteClient-Win64-Shipping.exe
    std::string error;            // user-facing message when !ok
};

struct ValidateResult {
    bool ok = false;
    std::string error;
    int checksPassed = 0;
    int checksTotal = 0;
};

struct StatusResult {
    bool registered = false;      // config.build.path set
    bool present = false;         // registered and game exe found on disk
    bool validated = false;       // validation passed at least once
    std::string path;
    std::string gameExePath;
    std::string buildId;
};

// Extract a downloaded ZIP (e.g. 4.10-CL-4053532.zip) into destDir using the
// Windows Shell. Extracts into "destDir\4.10-CL-4053532" (or destDir if the
// zip has no single root folder). Slow for tens of GB but requires nothing.
bool ExtractZip(const std::wstring& zipPath, const std::wstring& destDir,
                std::string& err);

// Register an already-on-disk build folder (user picked the folder, or we
// point at the extraction result). Validates key files before registering.
ImportResult ImportFolder(const std::wstring& folderUtf16);

// Re-check the currently registered build (files still there?).
ValidateResult ValidateRegistered();

// Combined status for the UI.
StatusResult Status();

// Path of the game exe for a given build folder ("" if absent).
std::wstring FindGameExe(const std::wstring& buildDir);

// Find the build root (folder containing FortniteGame) under a user-picked or
// extraction directory. Checks the folder itself plus a few standard nested
// layouts. Returns "" when no recognizable 4.10 build is present.
std::wstring LocateBuildRoot(const std::wstring& folder);

} // namespace build410
