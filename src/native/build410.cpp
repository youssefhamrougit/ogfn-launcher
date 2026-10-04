// build410.cpp — import, validate and register the single 4.10 build.
#include "build410.h"

#include "config.h"
#include "log.h"
#include "util.h"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <fstream>

namespace build410 {

using nlohmann::json;

std::wstring FindGameExe(const std::wstring& buildDir) {
    static const wchar_t* kCandidates[] = {
        L"\\FortniteGame\\Binaries\\Win64\\FortniteClient-Win64-Shipping.exe",
        L"\\FortniteGame\\Binaries\\Win64\\FortniteClient-Win64-Shipping_EAC.exe",
        L"\\FortniteGame\\Binaries\\Win64\\FortniteClient-Win64-Shipping_EAC_ez.dll.bak",
        L"\\FortniteGame\\Binaries\\Win64\\4.10-CL-4053532\\FortniteClient-Win64-Shipping.exe",
    };
    for (const wchar_t* suffix : kCandidates) {
        std::wstring p = buildDir + suffix;
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return {};
}

namespace {

// Shared sanity check: the 4 core folders every 4.10 build has.
bool CheckCoreFolders(const std::wstring& root, int& passed, int& total) {
    total = 4;
    passed = 0;
    static const wchar_t* kRequired[] = {
        L"\\FortniteGame\\Content",
        L"\\FortniteGame\\Binaries\\Win64",
        L"\\Engine\\Binaries\\ThirdParty",
        L"\\Engine\\Content",
    };
    for (const wchar_t* rel : kRequired) {
        if (GetFileAttributesW((root + rel).c_str()) != INVALID_FILE_ATTRIBUTES)
            passed++;
    }
    return passed == total;
}

} // namespace

// ------------------------------------------------------------------ zip

bool ExtractZip(const std::wstring& zipPath, const std::wstring& destDir,
                std::string& err) {
    if (GetFileAttributesW(zipPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = "ZIP file not found.";
        return false;
    }

    // The archive.org item "Fortnite-4.10-CL-4053532.zip" may contain the real
    // archive as an inner zip. Extract what we have; the import step then
    // recurses once if an inner zip appears.
    std::wstring outerDir = destDir + L"\\_zip";
    if (!util::EnsureDir(outerDir)) {
        err = "Could not create extraction folder.";
        return false;
    }

    // NOTE: SHFileOperationW with a .zip destination does NOT extract — it
    // silently copies the .zip file itself (verified). The Shell COM route in
    // util::ExtractZipShell is the supported extractor here.
    if (!util::ExtractZipShell(zipPath, outerDir, nullptr, nullptr, err)) {
        util::EnsureDir(outerDir); // keep partial output for diagnosis
        return false;
    }

    // If the outer zip contained an inner zip, extract that one level deeper.
    std::wstring inner = outerDir + L"\\4.10-CL-4053532.zip";
    if (GetFileAttributesW(inner.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return ExtractZip(inner, destDir, err); // recurse once
    }
    return true;
}

// ------------------------------------------------------------------ import

ImportResult ImportFolder(const std::wstring& folderUtf16) {
    ImportResult out;

    std::wstring buildRoot = LocateBuildRoot(folderUtf16);
    if (buildRoot.empty()) {
        out.error =
            "Could not find FortniteClient-Win64-Shipping.exe in that folder. "
            "Pick the folder that contains FortniteGame\\Binaries\\Win64.";
        return out;
    }

    // Basic sanity: check the files every 4.10 build has.
    int passed = 0, total = 0;
    if (!CheckCoreFolders(buildRoot, passed, total)) {
        out.error = "Build folder is incomplete (" + std::to_string(passed) +
                    "/" + std::to_string(total) +
                    " core folders present). The archive may be corrupted.";
        return out;
    }

    std::wstring exe = FindGameExe(buildRoot);

    // Register in config.
    config::SetBuild(util::ToUtf8(buildRoot), "imported", true);

    out.ok = true;
    out.gameExePath = util::ToUtf8(exe);
    return out;
}

// ---------------------------------------------------------------- validate

ValidateResult ValidateRegistered() {
    ValidateResult out;
    json cfg = config::Data();
    std::string path = cfg["build"]["path"].get<std::string>();
    if (path.empty()) {
        out.error = "No build is imported yet.";
        return out;
    }

    std::wstring root = util::ToUtf16(path);
    if (GetFileAttributesW(root.c_str()) == INVALID_FILE_ATTRIBUTES) {
        out.error = "The build folder is missing (moved or deleted?). Re-import it.";
        config::SetBuild(path, "none", false);
        return out;
    }

    if (FindGameExe(root).empty()) {
        out.error = "FortniteClient-Win64-Shipping.exe is missing from the build.";
        return out;
    }

    CheckCoreFolders(root, out.checksPassed, out.checksTotal);
    out.ok = (out.checksPassed == out.checksTotal);
    if (!out.ok)
        out.error = "Build is incomplete (" + std::to_string(out.checksPassed) +
                    "/" + std::to_string(out.checksTotal) + " core folders present).";
    return out;
}

// ------------------------------------------------------------------ status

StatusResult Status() {
    StatusResult out;
    json cfg = config::Data();
    out.path = cfg["build"]["path"].get<std::string>();
    out.validated = cfg["build"]["validated"].get<bool>();
    out.buildId = kBuildId;

    if (!out.path.empty()) {
        out.registered = true;
        std::wstring root = util::ToUtf16(out.path);
        std::wstring exe = FindGameExe(root);
        if (!exe.empty()) {
            out.present = true;
            out.gameExePath = util::ToUtf8(exe);
        }
    }
    return out;
}

// ------------------------------------------------------------- root locate

std::wstring LocateBuildRoot(const std::wstring& folder) {
    std::wstring exe = FindGameExe(folder);
    if (!exe.empty()) {
        // Root = the folder that contains FortniteGame.
        std::wstring low = exe;
        std::transform(low.begin(), low.end(), low.begin(), ::towlower);
        size_t pos = low.find(L"fortnitegame\\binaries");
        if (pos != std::wstring::npos && pos > 0) {
            std::wstring root = exe.substr(0, pos);
            while (!root.empty() && (root.back() == L'\\' || root.back() == L'/'))
                root.pop_back();
            return root;
        }
        return folder;
    }

    // One level down: user may have picked the parent that CONTAINS the build.
    static const wchar_t* kNested[] = {
        L"\\4.10-CL-4053532",
        L"\\Fortnite 4.10",
        L"\\Fortnite",
    };
    for (const wchar_t* nested : kNested) {
        std::wstring p = folder + nested;
        if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        exe = FindGameExe(p);
        if (!exe.empty()) {
            std::wstring low = exe;
            std::transform(low.begin(), low.end(), low.begin(), ::towlower);
            size_t pos = low.find(L"fortnitegame\\binaries");
            if (pos != std::wstring::npos && pos > 0) {
                std::wstring root = exe.substr(0, pos);
                while (!root.empty() && (root.back() == L'\\' || root.back() == L'/'))
                    root.pop_back();
                return root;
            }
            return p;
        }
    }
    return {};
}

} // namespace build410
