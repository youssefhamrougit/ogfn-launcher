// bridge.cpp — action dispatcher. main.cpp forwards WebView2 messages here.
#include "bridge.h"

#include "accounts.h"
#include "build410.h"
#include "config.h"
#include "crypto.h"
#include "log.h"
#include "util.h"

#include <windows.h>
#include <shellapi.h>

namespace bridge {

using nlohmann::json;

namespace {

json Ok(const json& id, const json& result = json::object()) {
    return json{{"id", id}, {"ok", true}, {"result", result}};
}

json Err(const json& id, const std::string& message) {
    return json{{"id", id}, {"ok", false}, {"error", message}};
}

json AccountsSnapshot() {
    accounts::Session s = accounts::Current();
    return json{
        {"signedIn", s.signedIn},
        {"username", s.username},
    };
}

json BuildStatusJson() {
    build410::StatusResult s = build410::Status();
    return json{
        {"registered", s.registered},
        {"present", s.present},
        {"validated", s.validated},
        {"path", s.path},
        {"gameExePath", s.gameExePath},
        {"buildId", s.buildId},
    };
}

// ---- action handlers -------------------------------------------------------

json Ping(const json&) {
    return json{{"pong", true}, {"version", OGFN_VERSION}};
}

json GetState(const json&) {
    json build = BuildStatusJson();
    build["launchBlocked"] = true; // multiplayer gate — see Next Phase note
    build["launchBlockReason"] =
        "Multiplayer launching arrives with the server phase. "
        "Your imported build is stored and validated locally.";
    return json{
        {"version", OGFN_VERSION},
        {"account", AccountsSnapshot()},
        {"config", config::Data()},
        {"build", build},
    };
}

json ConfigPatch(const json& payload) {
    if (!payload.is_object())
        return Err(json(), "config.patch expects an object payload");
    // Guard: some keys are native-managed.
    json safe = payload;
    safe.erase("launcher");
    return config::Patch(safe);
}

json AccountSignUp(const json& payload) {
    std::string err;
    accounts::Result r = accounts::SignUp(payload.value("username", ""),
                                          payload.value("password", ""), err);
    if (r != accounts::Result::Ok) return Err(json(), err);
    return AccountsSnapshot();
}

json AccountSignIn(const json& payload) {
    std::string err;
    accounts::Result r = accounts::SignIn(payload.value("username", ""),
                                          payload.value("password", ""), err);
    if (r != accounts::Result::Ok) return Err(json(), err);
    return AccountsSnapshot();
}

json AccountSignOut(const json&) {
    accounts::SignOut();
    return AccountsSnapshot();
}

json AccountChangePassword(const json& payload) {
    accounts::Session s = accounts::Current();
    std::string err;
    if (!s.signedIn) return Err(json(), "Not signed in.");
    if (!accounts::ChangePassword(s.username, payload.value("currentPassword", ""),
                                  payload.value("newPassword", ""), err))
        return Err(json(), err);
    return json{{"changed", true}};
}

json AccountDelete(const json& payload) {
    accounts::Session s = accounts::Current();
    std::string err;
    if (!s.signedIn) return Err(json(), "Not signed in.");
    if (!accounts::DeleteAccount(s.username, payload.value("password", ""), err))
        return Err(json(), err);
    return AccountsSnapshot();
}

json BuildImportFromZip(const json& payload) {
    std::wstring zip = util::ToUtf16(payload.value("zipPath", ""));
    if (zip.empty()) return Err(json(), "No ZIP path given.");

    // Extract next to the chosen file, into an "OGFN Builds" folder
    std::wstring zipDir = zip;
    size_t slash = zipDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) zipDir = zipDir.substr(0, slash);
    std::wstring dest = zipDir + L"\\OGFN Builds";
    util::EnsureDir(dest);

    std::string err;
    if (!build410::ExtractZip(zip, dest, err)) return Err(json(), err);

    // Prefer the standard folder name produced by the archive layout.
    std::wstring candidate = dest + L"\\4.10-CL-4053532";
    build410::ImportResult r;
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        r = build410::ImportFolder(candidate);
        if (!r.ok) {
            // Maybe the zip had a wrapper folder; try one level deeper.
            std::wstring oneIn = candidate + L"\\4.10-CL-4053532";
            if (GetFileAttributesW(oneIn.c_str()) != INVALID_FILE_ATTRIBUTES)
                r = build410::ImportFolder(oneIn);
        }
    } else {
        r = build410::ImportFolder(dest);
    }
    if (!r.ok) return Err(json(), r.error);
    return BuildStatusJson();
}

json BuildImportFromFolder(const json& payload) {
    std::wstring folder = util::ToUtf16(payload.value("folderPath", ""));
    if (folder.empty()) return Err(json(), "No folder path given.");
    build410::ImportResult r = build410::ImportFolder(folder);
    if (!r.ok) return Err(json(), r.error);
    return BuildStatusJson();
}

json BuildValidate(const json&) {
    build410::ValidateResult r = build410::ValidateRegistered();
    if (r.ok) {
        // Re-stamp validated state.
        json cfg = config::Data();
        config::SetBuild(cfg["build"]["path"].get<std::string>(), "valid", true);
    }
    return json{{"ok", r.ok},
                {"error", r.error},
                {"checksPassed", r.checksPassed},
                {"checksTotal", r.checksTotal}};
}

json BuildRemove(const json&) {
    // Un-register only; never deletes user files.
    config::SetBuild("", "none", false);
    return BuildStatusJson();
}

json PickZip(const json&) {
    std::string path = util::PickFileUtf8(
        L"Fortnite 4.10 archive (ZIP)\0*.zip\0All files\0*.*\0",
        /*save*/ false);
    return json{{"path", path}};
}

json PickFolder(const json&) {
    std::string path = util::PickFolderUtf8();
    return json{{"path", path}};
}

json PickZipSave(const json&) {
    std::string path = util::PickFileUtf8(
        L"ZIP archive\0*.zip\0", /*save*/ true);
    return json{{"path", path}};
}

json OpenInBrowser(const json& payload) {
    std::string url = payload.value("url", "");
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)
        return Err(json(), "Blocked non-http URL.");
    HINSTANCE r = ShellExecuteW(nullptr, L"open", util::ToUtf16(url).c_str(),
                                nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) return Err(json(), "Could not open browser.");
    return json{{"opened", true}};
}

json OpenDataFolder(const json&) {
    std::wstring dir = util::AppDataDir();
    util::EnsureDir(dir);
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return json{{"opened", true}};
}

json GetLogs(const json&) {
    std::string text;
    util::ReadTextFile(util::LogFilePath(), text);
    return json{{"logs", text}};
}

json ClearData(const json&) {
    // Sign out + wipe config to defaults. Accounts file is kept unless
    // payload.deleteAccounts is true.
    accounts::SignOut();
    config::Patch(json{{"build", {{"path", ""}, {"state", "none"},
                                  {"validated", false}, {"importedAt", ""}}}});
    return json{{"cleared", true}};
}

struct Handler {
    const char* name;
    json (*fn)(const json& payload);
};

const Handler kHandlers[] = {
    {"ping", Ping},
    {"state.get", GetState},
    {"config.patch", ConfigPatch},
    {"account.signUp", AccountSignUp},
    {"account.signIn", AccountSignIn},
    {"account.signOut", AccountSignOut},
    {"account.changePassword", AccountChangePassword},
    {"account.delete", AccountDelete},
    {"build.importFromZip", BuildImportFromZip},
    {"build.importFromFolder", BuildImportFromFolder},
    {"build.validate", BuildValidate},
    {"build.remove", BuildRemove},
    {"dialog.pickZip", PickZip},
    {"dialog.pickZipSave", PickZipSave},
    {"dialog.pickFolder", PickFolder},
    {"shell.openInBrowser", OpenInBrowser},
    {"shell.openDataFolder", OpenDataFolder},
    {"logs.get", GetLogs},
    {"logs.clearData", ClearData},
};

} // namespace

// ---------------------------------------------------------------- dispatch

nlohmann::json Handle(const nlohmann::json& request) {
    nlohmann::json id = nullptr;
    try {
        if (!request.is_object() || !request.contains("action"))
            return Err(nullptr, "Malformed request: missing 'action'.");

        id = request.at("id");
        std::string action = request["action"].get<std::string>();
        json payload = request.contains("payload") && request["payload"].is_object()
                           ? request["payload"]
                           : json::object();

        for (const Handler& h : kHandlers) {
            if (action == h.name) {
                json result = h.fn(payload);
                // Handlers that return errors already shaped {id, ok:false}.
                if (result.contains("ok") && result["ok"].is_boolean() &&
                    !result["ok"].get<bool>())
                    return result;
                return Ok(id, result);
            }
        }
        return Err(id, "Unknown action: " + action);
    } catch (const std::exception& e) {
        ogfnlog::Error(std::string("bridge: exception: ") + e.what());
        return Err(id, std::string("Internal error: ") + e.what());
    }
}

} // namespace bridge
