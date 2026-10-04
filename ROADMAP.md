# OGFN Launcher — Roadmap & Execution Plan

How this repo goes from empty to a working launcher.

**One phase = one commit.** Every phase leaves the repo in a state that builds.

---

## Scope

- **Fortnite build 4.10 only (CL 4053532, Season 4).** The launcher manages,
  validates, imports and (soon) launches exactly one build. Multi-build
  library management is explicitly out of scope.
- **Season 4 is multiplayer-only.** Launching stays disabled until the
  backend/server phase lands; the UI gates the Launch button behind the
  multiplayer phase.
- Windows 10/11, x64 only. Vanilla web UI, C++20 native core, backend-agnostic.

---

## Execution rules

1. Every phase must compile at the end of it. No half-broken states get committed.
2. Commit messages follow Conventional Commits: `type(scope): summary`.
3. Web UI stays framework-free (vanilla HTML/CSS/JS). Native code stays C++20 / Win32 / WebView2.

---

## Target project structure

```text
ogfn-launcher/
├── CMakeLists.txt          # Build recipe (MSVC + auto-fetched WebView2 SDK + JSON)
├── src/
│   ├── native/             # C++20 / Win32 / WebView2
│   │   ├── main.cpp        # Window + WebView2 host
│   │   ├── bridge.cpp/.h   # JS ⇄ C++ JSON message bridge
│   │   ├── config.cpp/.h   # Settings persistence (%APPDATA%)
│   │   ├── accounts.cpp/.h # Local accounts + session (PBKDF2)
│   │   ├── crypto.cpp/.h   # SHA-256 / PBKDF2 (CNG)
│   │   ├── build410.cpp/.h # 4.10 build import/validate (ZIP via Shell)
│   │   ├── download.cpp/.h # In-app downloader (WinHTTP) + install flow
│   │   ├── log.cpp/.h      # Structured logging
│   │   └── util.cpp/.h     # Paths, UTF conversion, COM dialogs/ZIP extract
│   └── web/                # Vanilla HTML / CSS / JS
│       ├── index.html      # Auth screen + Play / Setup / Settings pages
│       ├── css/
│       └── js/
├── installer/              # Inno Setup script (setup-app installer)
├── docs/bridge-protocol.md # Bridge message contract
└── .github/workflows/      # CI + releases (later)
```

---

## Step 1 — Skeleton: window + WebView2  ✅ DONE

**Goal:** a double-clickable `OGFNLauncher.exe` that opens a real window and renders a local HTML page.

| Phase | Scope | Status |
|---|---|---|
| 1.1 | CMake recipe, WebView2 SDK auto-fetch, gitignore | ✅ verified |
| 1.2 | Native host: Win32 window + WebView2 controller | ✅ verified |
| 1.3 | Placeholder shell page | ✅ verified |

---

## Step 2 — Web UI shell  ✅ DONE

| Phase | Scope | Status |
|---|---|---|
| 2.1 | Design tokens + base layout (dark/light CSS variables) | ✅ |
| 2.2 | App frame: sidebar nav + page routing | ✅ |
| 2.3 | Play, Setup & Download, Settings pages | ✅ |

---

## Step 3 — JS ⇄ C++ bridge  ✅ DONE

| Phase | Scope | Status |
|---|---|---|
| 3.1 | Message contract `{id, action, payload}` | ✅ `docs/bridge-protocol.md` |
| 3.2 | Native dispatch loop (`bridge.cpp`) | ✅ |
| 3.3 | Promise-based JS client (`js/bridge.js`) | ✅ |
| 3.4 | `ping` + full state actions end-to-end | ✅ verified |

---

## Step 4 — Config persistence  ✅ DONE

| Phase | Scope | Status |
|---|---|---|
| 4.1 | `%APPDATA%\OGFNLauncher\config.json` with deep-merge patching | ✅ |
| 4.2 | `config.patch` bridge action | ✅ |
| 4.3 | Settings page wiring (theme, accent) | ✅ |

---

## Step 5 — Accounts (local, backend-ready)  ✅ DONE (adapted)

> Decision: accounts work **locally** now (PBKDF2-hashed, stored in
> `%APPDATA%\OGFNLauncher\accounts.json`). The backend OAuth flow plugs into
> the same session state during the server phase.

| Phase | Scope | Status |
|---|---|---|
| 5.1 | Password hashing (PBKDF2-HMAC-SHA256 via CNG) | ✅ |
| 5.2 | Sign up / sign in / sign out / change password / delete | ✅ |
| 5.3 | Persistent session (restored on restart, tied to account) | ✅ |
| 5.4 | Auth screen (sign in / create account tabs) | ✅ |

---

## Step 6 — 4.10 build setup  ✅ DONE (adapted)

> Decision: the launcher does **not** download the build itself. The Setup
> page opens the archive URL in the user's browser; the user imports the
> downloaded ZIP and the launcher extracts + registers it.

| Phase | Scope | Status |
|---|---|---|
| 6.1 | Setup page with 4-step flow (browser download → import → validate) | ✅ |
| 6.2 | ZIP extraction (Windows Shell, handles nested archive layout) | ✅ |
| 6.3 | Folder import for already-extracted builds | ✅ |
| 6.4 | Build validation (core folders + game exe) and registration | ✅ |

---

## Step 7 — Downloads (in-app Library flow)  ✅ DONE (adapted)

> Decision: the Library tab downloads the build **inside the launcher**
> (WinHTTP, resumable via HTTP Range), then auto-installs it: extract →
> import → validate → registered. The browser-download route is kept on the
> Setup page as the manual alternative. The download URL is a placeholder
> until the Internet Archive item is finalized (`download.h: kBuildUrl`).

| Phase | Scope | Status |
|---|---|---|
| 7.1 | Library page (Epic-style card, progress bar, pause/resume/cancel) | ✅ |
| 7.2 | WinHTTP downloader: range resume, retry/backoff, progress events | ✅ |
| 7.3 | Auto-install after download (extract → import → validate) | ✅ |
| 7.4 | Completion notification + resume-after-restart (completed.flag) | ✅ |
| 7.5 | Swap placeholder URL for the final archive.org link | ⏳ waiting on link |

---

## Step 8 — Multiplayer readiness  ⬅️ NEXT (server phase)

**Goal:** a launched 4.10 client connects to the backend for multiplayer.
Launching is currently **disabled by design** (`build.launchBlocked: true`).

| Phase | Scope | Files |
|---|---|---|
| 8.1 | Backend OAuth token flow (HTTP client + token storage) | `http.cpp/.h`, `auth.cpp/.h` |
| 8.2 | Multiplayer launch with backend auth args (`-AUTH_LOGIN/-AUTH_PASSWORD/-AUTH_URL` style) — single-player launch is live via `game.launch` | `process.cpp/.h` |
| 8.3 | Gate multiplayer launch on a verified backend session | auth + process + web |
| 8.4 | Play page: session status, "connect to multiplayer" indicator | web |

**Verify:** launch with a logged-in backend session → game starts with auth
args; logged-out launch is blocked with a clear message.

---

## Later (post-core, unprioritized)

- Launcher self-updater + stable/beta channels
- Inno Setup CI workflow + signed releases (script already in `installer/`)
- News feed & backend status pages
- Backend account migration (link local accounts to server accounts)

---

## Current position

➡️ **Steps 1–7 complete.** The launcher builds, runs, and the full onboarding
flow works: create account → Library download → auto-install → validation →
single-player launch. Next: **Step 8 (server/multiplayer phase)**.
