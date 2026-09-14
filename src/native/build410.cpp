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

// ------------------------------------------------------------------ zip

bool ExtractZip(const std::wstring& zipPath, const std::wstring& destDir,
                std::string& err) {
    if (GetFileAttributesW(zipPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = "ZIP file not found.";
        return false;
    }

    // The archive.org item is "Fortnite-4.10-CL-4053532.zip" whose payload is
    // 4.10-CL-4053532.zip. Extract the outer zip into a staging dir first, then
    // use the inner zip if one appears.
    std::wstring outerDir = destDir + L"\\_zip";
    if (!util::EnsureDir(outerDir)) {
        err = "Could not create extraction folder.";
        return false;
    }

    wchar_t from[MAX_PATH + 4] = {};
    wchar_t to[MAX_PATH + 4] = {};
    wcsncpy_s(from, zipPath.c_str(), _TRUNCATE);
    wcsncpy_s(to, outerDir.c_str(), _TRUNCATE);

    // Path must be backslash-terminated for SHFileOperation.
    size_t tl = wcslen(to);
    to[tl] = L'\\';
    to[tl + 1] = L'\0';

    SHFILEOPSTRUCTW op = {};
    op.hwnd = nullptr;
    op.wFunc = FO_COPY;
    op.pFrom = from;
    op.pTo = to;
    op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT;
    int res = SHFileOperationW(&op);
    if (res != 0 || op.fAnyOperationsAborted) {
        err = "Windows could not extract the ZIP (error " + std::to_string(res) +
              ").";
        return false;
    }

    // If the outer zip contained an inner zip, extract that instead.
    std::wstring inner = outerDir + L"\\4.10-CL-4053532.zip";
    if (GetFileAttributesW(inner.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return ExtractZip(inner, destDir, err); // recurse once
    }

    // If extraction produced exactly one directory and no game exe at top
    // level, treat that directory as the build root.
    // (Handled by ImportFolder when the user picks the folder.)
    return true;
}

// ------------------------------------------------------------------ import

ImportResult ImportFolder(const std::wstring& folderUtf16) {
    ImportResult out;

    std::wstring exe = FindGameExe(folderUtf16);
    if (exe.empty()) {
        // Maybe the user picked the parent that CONTAINS the build folder.
        static const wchar_t* kNested[] = {
            L"\\4.10-CL-4053532",
            L"\\Fortnite 4.10",
            L"\\Fortnite",
        };
        for (const wchar_t* nested : kNested) {
            std::wstring p = folderUtf16 + nested;
            if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES)
                continue;
            exe = FindGameExe(p);
            if (!exe.empty()) break;
        }
    }

    if (exe.empty()) {
        out.error =
            "Could not find FortniteClient-Win64-Shipping.exe in that folder. "
            "Pick the folder that contains FortniteGame\\Binaries\\Win64.";
        return out;
    }

    // Basic sanity: check a few files every 4.10 build has.
    std::wstring buildRoot = folderUtf16;
    {
        // Determine the root (folder that contains FortniteGame).
        std::wstring low = exe;
        std::transform(low.begin(), low.end(), low.begin(), ::towlower);
        size_t pos = low.find(L"fortnitegame\\binaries");
        if (pos != std::wstring::npos && pos > 0)
            buildRoot = exe.substr(0, pos - (pos > 0 && exe[pos - 1] == L'\\' ? 1 : 0));
    }

    ValidateResult v;
    v.checksTotal = 4;
    const wchar_t* required[] = {
        L"\\FortniteGame\\Content",
        L"\\FortniteGame\\Binaries\\Win64",
        L"\\Engine\\Binaries\\ThirdParty",
        L"\\Engine\\Content",
    };
    for (const wchar_t* rel : required) {
        if (GetFileAttributesW((buildRoot + rel).c_str()) !=
            INVALID_FILE_ATTRIBUTES)
            v.checksPassed++;
    }
    v.ok = (v.checksPassed == v.checksTotal);
    if (!v.ok) {
        out.error = "Build folder is incomplete (" +
                    std::to_string(v.checksPassed) + "/" +
                    std::to_string(v.checksTotal) +
                    " core folders present). The archive may be corrupted.";
        return out;
    }

    // Register in config.
    config::SetBuild(util::ToUtf8(buildRoot), "imported", true);

    out.ok = true;
    out.gameExePath = util::ToUtf8(exe);
    return out;
}

// ------------------------------------------------------------------ validate

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

    out.checksTotal = 4;
    const wchar_t* required[] = {
        L"\\FortniteGame\\Content",
        L"\\FortniteGame\\Binaries\\Win64",
        L"\\Engine\\Binaries\\ThirdParty",
        L"\\Engine\\Content",
    };
    for (const wchar_t* rel : required) {
        if (GetFileAttributesW((root + rel).c_str()) != INVALID_FILE_ATTRIBUTES)
            out.checksPassed++;
    }
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

} // namespace build410
