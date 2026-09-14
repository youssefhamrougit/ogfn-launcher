// util.cpp — implementation of shared helpers.
#include "util.h"

#include <windows.h>
#include <shlobj.h>
#include <commdlg.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ctime>
#include <sstream>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif

namespace util {

static std::wstring BaseDataDir() {
    PWSTR raw = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw))) {
        dir = raw;
        CoTaskMemFree(raw);
    } else {
        // Extremely unlikely fallback.
        wchar_t buf[MAX_PATH];
        if (GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH) > 0) {
            dir = buf;
        } else {
            dir = L".";
        }
    }
    dir += L"\\OGFNLauncher";
    EnsureDir(dir);
    return dir;
}

std::wstring AppDataDir()      { return BaseDataDir(); }
std::wstring ConfigFilePath()  { return BaseDataDir() + L"\\config.json"; }
std::wstring AccountsFilePath(){ return BaseDataDir() + L"\\accounts.json"; }
std::wstring SessionFilePath() { return BaseDataDir() + L"\\session.json"; }
std::wstring LogDir()          { return BaseDataDir() + L"\\logs"; }
std::wstring LogFilePath()     { return LogDir() + L"\\launcher.log"; }

bool EnsureDir(const std::wstring& dir) {
    if (dir.empty()) return false;
    DWORD attr = GetFileAttributesW(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        return true;

    // Recurse on the parent first.
    std::wstring parent = dir;
    size_t slash = parent.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash > 0) {
        parent = parent.substr(0, slash);
        EnsureDir(parent);
    }
    return CreateDirectoryW(dir.c_str(), nullptr) ||
           (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES);
}

bool ReadTextFile(const std::wstring& path, std::string& out) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool WriteTextFile(const std::wstring& path, const std::string& content) {
    std::wstring dir = path;
    size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        dir = dir.substr(0, slash);
        if (!EnsureDir(dir)) return false;
    }
    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return f.good();
}

void DeleteFileSilent(const std::wstring& path) {
    DeleteFileW(path.c_str());
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring ToUtf16(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

bool IEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    }
    return true;
}

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::string IsoNow() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// ---------------------------------------------------------------- dialogs

// Uses raw COM interfaces (no CComPtr) so we don't need ATL.
std::string PickFileUtf8(const wchar_t* filter, bool saveDialog) {
    std::string result;

    IFileOpenDialog* open = nullptr;
    IFileSaveDialog* save = nullptr;
    HRESULT hr;
    if (saveDialog) {
        hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&save));
    } else {
        hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&open));
    }
    if (FAILED(hr)) return {};

    std::vector<std::wstring> patterns;
    std::vector<std::wstring> names;
    {
        // Parse null-separated filter: "Name\0*.zip\0Name2\0*.zip;*.7z\0\0"
        const wchar_t* p = filter;
        while (*p) {
            names.emplace_back(p);
            while (*p) ++p;
            ++p;
            patterns.emplace_back(p);
            while (*p) ++p;
            ++p;
        }
    }

    std::vector<COMDLG_FILTERSPEC> specs;
    for (size_t i = 0; i < names.size(); ++i) {
        specs.push_back({names[i].c_str(), patterns[i].c_str()});
    }

    if (open) {
        open->SetFileTypes((UINT)specs.size(), specs.data());
        if (!names.empty()) open->SetFileTypeIndex(1);
        if (SUCCEEDED(open->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(open->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) &&
                    path) {
                    result = util::ToUtf8(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        open->Release();
    } else if (save) {
        save->SetFileTypes((UINT)specs.size(), specs.data());
        if (!names.empty()) save->SetDefaultExtension(L"zip");
        if (SUCCEEDED(save->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(save->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) &&
                    path) {
                    result = util::ToUtf8(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        save->Release();
    }
    return result;
}

std::string PickFolderUtf8() {
    std::string result;
    IFileDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return {};
    DWORD options = 0;
    if (SUCCEEDED(dlg->GetOptions(&options)))
        dlg->SetOptions(options | FOS_PICKFOLDERS);
    if (SUCCEEDED(dlg->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = util::ToUtf8(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

} // namespace util
