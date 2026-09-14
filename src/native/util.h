// util.h — small shared helpers (paths, formatting).
#pragma once

#include <string>

namespace util {

// %APPDATA%\OGFNLauncher  (created on demand)
std::wstring AppDataDir();

// %APPDATA%\OGFNLauncher\config.json
std::wstring ConfigFilePath();

// %APPDATA%\OGFNLauncher\accounts.json
std::wstring AccountsFilePath();

// %APPDATA%\OGFNLauncher\session.json
std::wstring SessionFilePath();

// %APPDATA%\OGFNLauncher\logs
std::wstring LogDir();

// %APPDATA%\OGFNLauncher\logs\launcher.log
std::wstring LogFilePath();

// Read a whole text file (UTF-8). Returns false if it cannot be opened.
bool ReadTextFile(const std::wstring& path, std::string& out);

// Write a text file (UTF-8). Creates parent directories.
bool WriteTextFile(const std::wstring& path, const std::string& content);

// Delete a file if it exists (ignore errors).
void DeleteFileSilent(const std::wstring& path);

// Ensure a directory exists (recursive). Returns true if it exists afterwards.
bool EnsureDir(const std::wstring& dir);

// UTF-8 <-> UTF-16 conversion.
std::string  ToUtf8(const std::wstring& w);
std::wstring ToUtf16(const std::string& s);

// Case-insensitive compare for ASCII identifiers (usernames, keys).
bool IEquals(const std::string& a, const std::string& b);

// Trim ASCII whitespace.
std::string Trim(const std::string& s);

// ISO-8601 UTC timestamp, e.g. "2026-09-14T12:34:56Z".
std::string IsoNow();

// Native file dialogs. Return "" when cancelled.
// filterExample: e.g. L"ZIP archive\0*.zip\0All files\0*.*\0"
std::string PickFileUtf8(const wchar_t* filter, bool saveDialog);
std::string PickFolderUtf8();

} // namespace util
