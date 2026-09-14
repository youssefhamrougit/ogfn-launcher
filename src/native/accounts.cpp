// accounts.cpp — local accounts with PBKDF2-hashed passwords + session file.
#include "accounts.h"

#include "crypto.h"
#include "log.h"
#include "util.h"

#include <nlohmann/json.hpp>

#include <mutex>

namespace accounts {

using nlohmann::json;

static std::mutex g_mutex;
static json g_users = json::array();
static Session g_session;

// ---------------------------------------------------------------- helpers

static bool ValidUsername(const std::string& u) {
    if (u.size() < 3 || u.size() > 24) return false;
    for (char c : u) {
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-'))
            return false;
    }
    return true;
}

static bool ValidPassword(const std::string& p) {
    return p.size() >= 8 && p.size() <= 128;
}

static const json* FindUser(const std::string& username) {
    for (const auto& u : g_users) {
        if (u.contains("username") && util::IEquals(u["username"], username))
            return &u;
    }
    return nullptr;
}

static bool SaveUsersLocked() {
    json doc{{"version", 1}, {"users", g_users}};
    return util::WriteTextFile(util::AccountsFilePath(), doc.dump(2));
}

static void LoadUsersLocked() {
    g_users = json::array();
    std::string text;
    if (!util::ReadTextFile(util::AccountsFilePath(), text) || text.empty())
        return;
    try {
        json doc = json::parse(text);
        if (doc.contains("users") && doc["users"].is_array())
            g_users = doc["users"];
    } catch (const std::exception& e) {
        ogfnlog::Warn(std::string("accounts: parse failed: ") + e.what());
    }
}

static void LoadSessionLocked() {
    g_session = {};
    std::string text;
    if (!util::ReadTextFile(util::SessionFilePath(), text) || text.empty())
        return;
    try {
        json doc = json::parse(text);
        g_session.signedIn = doc.value("signedIn", false);
        g_session.username = doc.value("username", "");
        g_session.createdAt = doc.value("createdAt", "");
        // Session must reference a still-existing account.
        if (g_session.signedIn && !FindUser(g_session.username)) {
            g_session = Session{};
            util::DeleteFileSilent(util::SessionFilePath());
        }
    } catch (...) {
        g_session = Session{};
    }
}

static void WriteSessionLocked() {
    if (g_session.signedIn) {
        json doc{{"signedIn", true},
                 {"username", g_session.username},
                 {"createdAt", g_session.createdAt},
                 {"token", crypto::RandomHex(32)}};
        util::WriteTextFile(util::SessionFilePath(), doc.dump(2));
    } else {
        util::DeleteFileSilent(util::SessionFilePath());
    }
}

// ---------------------------------------------------------------- public API

void Init() {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadUsersLocked();
    LoadSessionLocked();
    ogfnlog::Info("accounts: loaded " + std::to_string(g_users.size()) +
                  " account(s)" +
                  (g_session.signedIn ? ", session restored for " + g_session.username
                                      : ", signed out"));
}

Result SignUp(const std::string& usernameRaw, const std::string& password,
              std::string& err) {
    std::string username = util::Trim(usernameRaw);
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!ValidUsername(username)) {
        err = "Username must be 3-24 characters (letters, numbers, - or _).";
        return Result::InvalidInput;
    }
    if (!ValidPassword(password)) {
        err = "Password must be at least 8 characters.";
        return Result::WeakPassword;
    }
    if (FindUser(username)) {
        err = "That username is already taken.";
        return Result::UsernameTaken;
    }

    json user{{"username", username},
              {"passHash", crypto::HashPassword(password)},
              {"createdAt", util::IsoNow()}};
    if (user["passHash"].get<std::string>().empty()) {
        err = "Could not hash password (internal error).";
        return Result::IoError;
    }
    g_users.push_back(user);
    if (!SaveUsersLocked()) {
        auto last = g_users.end();
        --last;
        g_users.erase(last);
        err = "Could not save account file.";
        return Result::IoError;
    }

    // Sign the new user in immediately.
    g_session = Session{true, username, util::IsoNow()};
    WriteSessionLocked();

    ogfnlog::Info("accounts: created account '" + username + "'");
    return Result::Ok;
}

Result SignIn(const std::string& usernameRaw, const std::string& password,
              std::string& err) {
    std::string username = util::Trim(usernameRaw);
    std::lock_guard<std::mutex> lock(g_mutex);

    const json* user = FindUser(username);
    if (!user) {
        err = "No account with that username.";
        return Result::NoSuchUser;
    }
    if (!crypto::VerifyPassword(password, (*user)["passHash"])) {
        err = "Incorrect password.";
        return Result::WrongPassword;
    }

    g_session = Session{true, (*user)["username"], util::IsoNow()};
    WriteSessionLocked();

    ogfnlog::Info("accounts: signed in '" + g_session.username + "'");
    return Result::Ok;
}

bool SignOut() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_session.signedIn) return true;
    ogfnlog::Info("accounts: signed out '" + g_session.username + "'");
    g_session = Session{};
    WriteSessionLocked();
    return true;
}

Session Current() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_session;
}

bool ChangePassword(const std::string& username,
                    const std::string& currentPassword,
                    const std::string& newPassword, std::string& err) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const json* user = FindUser(username);
    if (!user) {
        err = "Account not found.";
        return false;
    }
    if (!crypto::VerifyPassword(currentPassword, (*user)["passHash"])) {
        err = "Current password is incorrect.";
        return false;
    }
    if (!ValidPassword(newPassword)) {
        err = "New password must be at least 8 characters.";
        return false;
    }
    // json points into g_users; mutate via index to keep references valid.
    for (auto& u : g_users) {
        if (util::IEquals(u["username"], username)) {
            u["passHash"] = crypto::HashPassword(newPassword);
            break;
        }
    }
    if (!SaveUsersLocked()) {
        err = "Could not save account file.";
        return false;
    }
    return true;
}

bool DeleteAccount(const std::string& username, const std::string& password,
                   std::string& err) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const json* user = FindUser(username);
    if (!user) {
        err = "Account not found.";
        return false;
    }
    if (!crypto::VerifyPassword(password, (*user)["passHash"])) {
        err = "Password is incorrect.";
        return false;
    }
    for (auto it = g_users.begin(); it != g_users.end(); ++it) {
        if (util::IEquals((*it)["username"], username)) {
            g_users.erase(it);
            break;
        }
    }
    if (!SaveUsersLocked()) {
        err = "Could not save account file.";
        return false;
    }
    g_session = Session{};
    WriteSessionLocked();
    ogfnlog::Info("accounts: deleted account '" + username + "'");
    return true;
}

} // namespace accounts
