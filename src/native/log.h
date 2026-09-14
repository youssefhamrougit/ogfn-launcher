// log.h — structured logging to %APPDATA%\OGFNLauncher\logs\launcher.log
#pragma once

#include <string>

namespace ogfnlog {

enum class Level { Debug, Info, Warn, Error };

// Open/rotate the log file. Called once at startup.
void Init();

// Append a line. Thread-safe.
void Write(Level level, const std::string& message);

// Convenience wrappers.
void Debug(const std::string& msg);
void Info(const std::string& msg);
void Warn(const std::string& msg);
void Error(const std::string& msg);

} // namespace ogfnlog
