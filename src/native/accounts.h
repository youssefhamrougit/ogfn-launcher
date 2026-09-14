// accounts.h — local account management + session (offline-capable).
// Passwords are stored PBKDF2-hashed (see crypto.h). The backend OAuth flow
// plugs into the same session state in the server phase.
#pragma once

#include <string>
#include <vector>

namespace accounts {

enum class Result {
    Ok,
    InvalidInput,
    WeakPassword,
    UsernameTaken,
    NoSuchUser,
    WrongPassword,
    IoError,
};

struct Session {
    bool signedIn = false;
    std::string username;
    std::string createdAt;
};

// Load accounts.json + restore an existing session if present.
void Init();

Result SignUp(const std::string& username, const std::string& password,
              std::string& err);

Result SignIn(const std::string& username, const std::string& password,
              std::string& err);

bool SignOut();

// Current session (persisted across restarts).
Session Current();

bool ChangePassword(const std::string& username,
                    const std::string& currentPassword,
                    const std::string& newPassword, std::string& err);

bool DeleteAccount(const std::string& username, const std::string& password,
                   std::string& err);

} // namespace accounts
