// config.cpp — JSON-backed settings with deep-merge patching.
#include "config.h"

#include "log.h"
#include "util.h"

#include <mutex>

namespace config {

static std::mutex g_mutex;
static nlohmann::json g_data;

static nlohmann::json Defaults() {
    return nlohmann::json{
        {"theme", "dark"},                    // dark | light | system
        {"accent", "#3b82f6"},
        {"build",
         {{"path", ""},
          {"state", "none"},                  // none | imported | valid
          {"validated", false},
          {"importedAt", ""}}},
        {"game",
         {{"multiplayerOnly", true},          // Season 4 is multiplayer-only
          {"buildId", "4.10-CL-4053532"}}},
        {"launcher", {{"version", OGFN_VERSION}}},
    };
}

static void MergeInto(nlohmann::json& dst, const nlohmann::json& src) {
    for (auto it = src.begin(); it != src.end(); ++it) {
        if (it.value().is_object() && dst.contains(it.key()) &&
            dst[it.key()].is_object()) {
            MergeInto(dst[it.key()], it.value());
        } else {
            dst[it.key()] = it.value();
        }
    }
}

void Load() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_data = Defaults();

    std::string text;
    if (util::ReadTextFile(util::ConfigFilePath(), text) && !text.empty()) {
        try {
            nlohmann::json stored = nlohmann::json::parse(text);
            if (stored.is_object()) MergeInto(g_data, stored);
        } catch (const std::exception& e) {
            ogfnlog::Warn(std::string("config: parse failed, using defaults: ") +
                          e.what());
        }
    }
}

nlohmann::json Patch(const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (patch.is_object()) {
        MergeInto(g_data, patch);
        util::WriteTextFile(util::ConfigFilePath(), g_data.dump(2));
    }
    return g_data;
}

nlohmann::json Data() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_data;
}

void SetBuild(const std::string& pathUtf8, const std::string& state,
              bool validated) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_data["build"]["path"] = pathUtf8;
    g_data["build"]["state"] = state;
    g_data["build"]["validated"] = validated;
    g_data["build"]["importedAt"] = util::IsoNow();
    util::WriteTextFile(util::ConfigFilePath(), g_data.dump(2));
    ogfnlog::Info("config: build registered at " + pathUtf8 +
                  " state=" + state);
}

} // namespace config
