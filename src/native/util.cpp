// util.cpp — implementation of shared helpers.
#include "util.h"

#include "log.h"

#include <windows.h>
#include <shlwapi.h>
#include <shldisp.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <objbase.h>

#include <algorithm>
#include <cctype>
#include <functional>
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
                    result = ToUtf8(path);
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
                    result = ToUtf8(path);
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
                result = ToUtf8(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

// ------------------------------------------------------------- zip extract
//
// Extraction uses the Shell Folder dual interfaces (the same COM path behind
// Explorer's "Extract All"). SHFileOperationW with a .zip target does NOT
// extract — it silently copies the .zip itself (verified on Windows 11), so
// Folder::CopyHere is the supported route here.
//
// Progress: bytes that have landed in destDir, measured against (a) the
// shell-reported size of the item being extracted, or (b) — when the item
// size is unknown — the zip file size itself. Cancellation: the caller's
// predicate is polled ~4x/second; a cancel takes effect at the next poll.

unsigned long long DirSizeBytes(const std::wstring& dir) {
    unsigned long long total = 0;
    std::vector<std::wstring> stack{dir};
    while (!stack.empty()) {
        std::wstring cur = stack.back();
        stack.pop_back();
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW((cur + L"\\*").c_str(), FindExInfoBasic,
                                    &fd, FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (wcscmp(fd.cFileName, L".") == 0 ||
                wcscmp(fd.cFileName, L"..") == 0)
                continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                stack.push_back(cur + L"\\" + fd.cFileName);
            } else {
                total += (static_cast<unsigned long long>(fd.nFileSizeHigh) << 32) |
                         fd.nFileSizeLow;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return total;
}

namespace {

// Parse a number that may arrive as "27,123,456,789" / "27.123.456.789" /
// VT_BSTR / VT_I4 / VT_I8 / VT_R8 from ExtendedProperty("size").
unsigned long long VariantToBytes(const VARIANT& v) {
    switch (v.vt) {
        case VT_I4:  return (unsigned long long)(v.lVal > 0 ? v.lVal : 0);
        case VT_UI4: return (unsigned long long)v.ulVal;
        case VT_I8:  return (unsigned long long)(v.llVal > 0 ? v.llVal : 0);
        case VT_UI8: return (unsigned long long)v.ullVal;
        case VT_R8:  return (unsigned long long)(v.dblVal > 0 ? v.dblVal : 0);
        case VT_BSTR: {
            unsigned long long n = 0;
            const wchar_t* p = v.bstrVal;
            for (; p && *p; ++p) {
                if (*p >= L'0' && *p <= L'9') n = n * 10 + (unsigned)(*p - L'0');
            }
            return n;
        }
        default: return 0;
    }
}

} // namespace

bool ExtractZipShell(const std::wstring& zipPath, const std::wstring& destDir,
                     std::function<bool()> cancel,
                     std::function<void(double)> progress,
                     std::string& err) {
    auto report = [&](double p) {
        if (progress) progress(p < 0 ? 0 : p > 1 ? 1 : p);
    };
    report(0.0);

    if (GetFileAttributesW(zipPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = "Archive file not found.";
        return false;
    }

    // Per-process Shell singleton.
    IShellDispatch* dispatch = nullptr;
    {
        IDispatch* d = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&d));
        if (SUCCEEDED(hr) && d) {
            d->QueryInterface(IID_PPV_ARGS(&dispatch));
            d->Release();
        }
        if (!dispatch) {
            err = "Windows Shell is unavailable right now.";
            return false;
        }
    }

    VARIANT vZip;
    VariantInit(&vZip);
    vZip.vt = VT_BSTR;
    vZip.bstrVal = SysAllocString(zipPath.c_str());
    VARIANT vDest;
    VariantInit(&vDest);
    vDest.vt = VT_BSTR;
    vDest.bstrVal = SysAllocString(destDir.c_str());

    Folder* zipFolder = nullptr;
    Folder* destFolder = nullptr;
    FolderItems* items = nullptr;
    bool cancelled = false;

    auto cleanup = [&]() {
        if (items) items->Release();
        if (destFolder) destFolder->Release();
        if (zipFolder) zipFolder->Release();
        VariantClear(&vZip);
        VariantClear(&vDest);
        dispatch->Release();
    };

    HRESULT hr = dispatch->NameSpace(vZip, &zipFolder);
    if (FAILED(hr) || !zipFolder) {
        cleanup();
        err = "The file could not be opened as a ZIP archive (is it still "
              "downloading, or corrupted?).";
        return false;
    }
    if (!EnsureDir(destDir)) {
        cleanup();
        err = "Could not create the extraction folder.";
        return false;
    }
    hr = dispatch->NameSpace(vDest, &destFolder);
    if (FAILED(hr) || !destFolder) {
        cleanup();
        err = "Could not open the extraction folder.";
        return false;
    }
    hr = zipFolder->Items(&items);
    if (FAILED(hr) || !items) {
        cleanup();
        err = "The archive could not be read (corrupt or empty?).";
        return false;
    }

    long itemCount = 0;
    items->get_Count(&itemCount);
    if (itemCount <= 0) {
        cleanup();
        err = "The archive is empty.";
        return false;
    }

    // Rough size reference for progress when an item's size is unknown:
    // game zips store already-compressed pak data, so extracted bytes track
    // the zip size closely.
    unsigned long long zipBytes = DirSizeBytes(zipPath);

    // CopyHere flags: 4 = no "replace?" dialog, 16 = yes-to-all,
    // 512 = suppress error dialogs, 1024 = no "create folder?" dialog.
    const LONG kCopyFlags = 4 | 16 | 512 | 1024;

    unsigned long long baseline = DirSizeBytes(destDir);

    for (long i = 0; i < itemCount; ++i) {
        if (cancel && cancel()) {
            cancelled = true;
            break;
        }

        VARIANT vIndex;
        VariantInit(&vIndex);
        vIndex.vt = VT_I4;
        vIndex.lVal = i;
        FolderItem* item = nullptr;
        hr = items->Item(vIndex, &item);
        VariantClear(&vIndex);
        if (FAILED(hr) || !item) continue;

        // Log the entry name for diagnosis.
        {
            BSTR name = nullptr;
            if (SUCCEEDED(item->get_Name(&name)) && name) {
                ogfnlog::Info("extract: item " + std::to_string(i + 1) + "/" +
                              std::to_string(itemCount) + " \"" +
                              ToUtf8(std::wstring(name)) + "\"");
                SysFreeString(name);
            }
        }

        // Item size (folder items aggregate their children). ExtendedProperty
        // returns a 64-bit-capable VARIANT; get_Size caps at 2^31.
        unsigned long long itemTotal = 0;
        {
            FolderItem2* item2 = nullptr;
            if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&item2))) && item2) {
                BSTR propName = SysAllocString(L"size");
                VARIANT vSize;
                VariantInit(&vSize);
                if (SUCCEEDED(item2->ExtendedProperty(propName, &vSize))) {
                    itemTotal = VariantToBytes(vSize);
                    VariantClear(&vSize);
                }
                SysFreeString(propName);
                item2->Release();
            }
            if (itemTotal == 0) {
                LONG sz = 0;
                if (SUCCEEDED(item->get_Size(&sz)) && sz > 0)
                    itemTotal = (unsigned long long)sz;
            }
        }

        // Hand the item to the destination folder (this extracts it).
        // The variant borrows `item` (no AddRef) — detach after the call so
        // VariantClear doesn't release it; our own item->Release() below is
        // the single owner.
        VARIANT vItem;
        VariantInit(&vItem);
        vItem.vt = VT_DISPATCH;
        vItem.pdispVal = item;
        VARIANT vFlags;
        VariantInit(&vFlags);
        vFlags.vt = VT_I4;
        vFlags.lVal = kCopyFlags;
        destFolder->CopyHere(vItem, vFlags);
        vItem.vt = VT_EMPTY;
        vItem.pdispVal = nullptr;
        VariantClear(&vItem);
        VariantClear(&vFlags);

        // Poll until the destination stops growing and stays stable.
        unsigned long long last = 0;
        int stableTicks = 0;
        const int kStableNeeded = 6;       // 6 * 250ms = 1.5s of quiet
        const int kMaxTicks = 60 * 60 * 4; // 1h hard cap per item
        for (int tick = 0; tick < kMaxTicks; ++tick) {
            Sleep(250);
            if (cancel && cancel()) {
                cancelled = true;
                break;
            }
            unsigned long long nowBytes = DirSizeBytes(destDir);
            unsigned long long done = nowBytes > baseline ? nowBytes - baseline : 0;

            if (itemTotal > 0) {
                if (done > itemTotal) done = itemTotal;
                report(((double)i + (double)done / (double)itemTotal) /
                       (double)itemCount);
            } else if (zipBytes > 0) {
                double est = (double)nowBytes / (double)zipBytes;
                report(est > 0.95 ? 0.95 : est); // estimate; finished at 1.0
            }

            if (itemTotal > 0 && done >= itemTotal && stableTicks >= 2) break;
            if (nowBytes == last) {
                if (++stableTicks >= kStableNeeded) break;
            } else {
                stableTicks = 0;
                last = nowBytes;
            }
        }

        item->Release();
        baseline = DirSizeBytes(destDir);
        if (cancelled) break;
    }

    items->Release();
    items = nullptr;
    cleanup();

    if (cancelled) {
        err = "Extraction cancelled.";
        return false;
    }
    report(1.0);
    return true;
}

} // namespace util
