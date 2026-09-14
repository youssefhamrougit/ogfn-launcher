// config.h — launcher settings persistence (%APPDATA%\OGFNLauncher\config.json)
#pragma once

#include <nlohmann/json.hpp>

namespace config {

// Load config from disk (or defaults on first run). Called once at startup.
void Load();

// Deep-merge `patch` into the stored config and persist it.
// Returns the full config after the merge.
nlohmann::json Patch(const nlohmann::json& patch);

// Full config snapshot (for bridge / other modules).
nlohmann::json Data();

// Convenience setters used by the build import flow.
void SetBuild(const std::string& pathUtf8, const std::string& state,
              bool validated);

} // namespace config
