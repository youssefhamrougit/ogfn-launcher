# OGFN Launcher — Roadmap & Execution Plan

How this repo goes from empty to a working launcher.

**One phase = one commit.** Code is written only after a phase is approved,
and every phase leaves the repo in a state that builds.

---

## Scope

- **Fortnite build 4.10 only.** The launcher manages, validates, downloads and launches exactly one build: 4.10. Multi-build library management is explicitly out of scope.
- **Multiplayer is near-term.** Launches must pass backend auth to the game so multiplayer sessions work (see Step 8, planned as part of the core, not "later").
- Windows 10/11, x64 only. Vanilla web UI, C++20 native core, backend-agnostic.

---

## Execution rules

1. **Propose → approve → code → verify → commit → next phase.** Nothing is executed without approval.
2. Every phase must compile at the end of it. No half-broken states get committed.
3. Commit messages follow Conventional Commits: `type(scope): summary`.
4. Web UI stays framework-free (vanilla HTML/CSS/JS). Native code stays C++20 / Win32 / WebView2.
5. I (the agent) write code; you review and commit/push yourself.

---

## Target project structure

```text
ogfn-launcher/
├── CMakeLists.txt          # Build recipe (MSVC + auto-fetched WebView2 SDK)
├── src/
│   ├── native/             # C++20 / Win32 / WebView2
│   │   ├── main.cpp        # Window + WebView2 host
│   │   ├── bridge.cpp/.h   # JS ⇄ C++ JSON message bridge
│   │   ├── config.cpp/.h   # Settings persistence (%APPDATA%)
│   │   ├── http.cpp/.h     # Async HTTP client
│   │   ├── auth.cpp/.h     # OAuth token flow
│   │   ├── build410.cpp/.h # 4.10 build import/validate
│   │   ├── process.cpp/.h  # Launch + monitor (auth args) Fortnite
│   │   ├── downloads.cpp/.h# Chunked resumable downloads (4.10 manifest)
│   │   └── crypto.cpp/.h   # SHA-256 verification
│   └── web/                # Vanilla HTML / CSS / JS
│       ├── index.html
│       ├── css/
│       └── js/
├── installer/              # Inno Setup (later)
├── docs/                   # Bridge protocol, docs
└── .github/workflows/      # CI + releases (later)
```

---

## Step 1 — Skeleton: window + WebView2  🚧 IN PROGRESS

**Goal:** a double-clickable `OGFNLauncher.exe` that opens a real window and renders a local HTML page. This de-risks the toolchain before any feature work.

| Phase | Scope | Files | Commit | Status |
|---|---|---|---|---|
| 1.1 | Build foundation: CMake recipe, WebView2 SDK auto-fetch, gitignore | `CMakeLists.txt`, `.gitignore` | `chore: add CMake build with WebView2 SDK fetch` | ✅ verified |
| 1.2 | Native host: Win32 window + WebView2 controller loading `web/index.html` | `src/native/main.cpp` | `feat(native): add Win32 window hosting WebView2` | ⏳ awaiting approval |
| 1.3 | Placeholder shell: minimal page proving the host works | `src/web/index.html` | `feat(web): add placeholder shell for WebView2 host` | ⏳ awaiting approval |

**Verify:** `cmake -B build` then `cmake --build build --config Release` → run exe → placeholder shows "WebView2 OK".

---

## Step 2 — Web UI shell

**Goal:** real app frame — sidebar nav, pages, theming. Still 100% static, no native calls. Pages reflect the 4.10-only flow: **Play**, **Setup**, **Settings**.

| Phase | Scope | Files | Commit |
|---|---|---|---|
| 2.1 | Design tokens + base layout (dark/light CSS variables) | `src/web/css/base.css` | `feat(web): add design tokens and base layout` |
| 2.2 | App frame: sidebar nav + hash-based page router | `index.html`, `js/app.js` | `feat(web): add app frame with page navigation` |
| 2.3 | Stub pages: Play, Setup, Settings | `js/pages/*.js` | `feat(web): add stub pages for main sections` |

**Verify:** click through all pages; no console errors.

---

## Step 3 — JS ⇄ C++ bridge  *(backbone — design carefully)*

**Goal:** one versioned JSON message protocol between UI and native. Every later feature rides on it.

| Phase | Scope | Files | Commit |
|---|---|---|---|
| 3.1 | Message contract: `{id, action, payload}` + response/error shapes | `docs/bridge-protocol.md` | `docs: define bridge message protocol` |
| 3.2 | Native host object + dispatch loop | `src/native/bridge.cpp/.h` | `feat(native): add bridge host object and dispatcher` |
| 3.3 | JS client wrapper with promise-based calls | `src/web/js/bridge.js` | `feat(web): add promise-based bridge client` |
| 3.4 | `ping` action end-to-end (proof the pipe works) | both sides | `feat: add ping action as end-to-end bridge test` |

**Verify:** button in UI → native round-trip → response rendered.

---

## Step 4 — Config persistence

**Goal:** settings survive restarts.

| Phase | Scope | Files | Commit |
|---|---|---|---|
| 4.1 | Load/save `%APPDATA%\OGFNLauncher\config.json` | `src/native/config.cpp/.h` | `feat(native): add config load and save` |
| 4.2 | Bridge actions `config.get` / `config.set` | bridge | `feat(bridge): expose config get and set` |
| 4.3 | Settings page wiring (backend URL, 4.10 path, theme) | web | `feat(web): wire settings page to config` |

**Verify:** change a setting, restart launcher, value persists.

---

## Step 5 — Backend login flow

**Goal:** first real integration — configure backend URL, log in, verified session.

| Phase | Scope | Files | Commit |
|---|---|---|---|
| 5.1 | Async HTTP client (WinHTTP) | `src/native/http.cpp/.h` | `feat(native): add async http client` |
| 5.2 | `POST /account/api/oauth/token` + token storage | `src/native/auth.cpp/.h` | `feat(native): add oauth token flow` |
| 5.3 | `GET /account/api/oauth/verify` + session state | auth + bridge | `feat: verify session on login and startup` |
| 5.4 | Login UI (URL + credentials, status display) | web | `feat(web): add login flow UI` |

**Verify:** log in against a backend; token persists; verify endpoint returns OK.

---

## Step 6 — 4.10 build setup & launching

**Goal:** import and validate the 4.10 build, then launch it. Single build — no library management.

| Phase | Scope | Files | Commit |
|---|---|---|---|
| 6.1 | Import 4.10 build (folder pick, scan, register) | `src/native/build410.cpp/.h` | `feat(native): add 4.10 build import and scan` |
| 6.2 | Validate 4.10 layout (key files/hashes present) | `build410.cpp` | `feat(native): validate 4.10 build files` |
| 6.3 | Setup page UI + launch button state | web | `feat(web): add build setup page` |
| 6.4 | Process launch + basic monitoring | `src/native/process.cpp/.h` | `feat(native): add build launch and monitor` |

**Verify:** import a 4.10 folder, validation passes, launch starts the game process.

---

## Step 7 — Downloads (single 4.10 manifest)

**Goal:** fetch the known 4.10 build resiliently — tens of GB, interruption-safe.

| Phase | Scope | Files | Commit |
|---|---|---|---|
| 7.1 | Chunked downloader with resume | `src/native/downloads.cpp/.h` | `feat(native): add chunked resumable downloader` |
| 7.2 | SHA-256 verification | `src/native/crypto.cpp/.h` | `feat(native): add sha-256 verification` |
| 7.3 | 4.10 manifest handling + live progress events to UI | bridge + web | `feat: add 4.10 manifest downloads with live progress` |

**Verify:** download against a small test manifest; kill mid-download; resume completes; hash checks out.

---

## Step 8 — Multiplayer readiness  ⬆️ pulled forward (near-term)

**Goal:** a launched 4.10 client actually connects to the backend for multiplayer.

## Current position

➡️ **Step 1, Phase 1.1** — verified. Next: approve Phase 1.2.
