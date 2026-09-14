<div align="center">

# OGFN Launcher

**A simple, lightweight launcher for OGFN (OG Fortnite) private servers — Season 4 only.**

[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)](#requirements)
[![Build](https://img.shields.io/badge/build-4.10%20%28CL%204053532%29-orange)](#installing-the-410-build)
[![License](https://img.shields.io/github/license/OWNER/REPO)](LICENSE)

Built with **C++ and vanilla HTML/CSS/JS**.
No Electron. No big frameworks. Just a small native launcher.

</div>

---

## What is OGFN Launcher?

OGFN Launcher is an open-source launcher for **OG Fortnite private servers**,
built for **Fortnite build 4.10 (CL 4053532) — Season 4**.

The launcher is **multiplayer-first**: Season 4 is played on private servers,
so launching stays disabled until your account and build are ready for the
backend phase (in development).

The interface is regular HTML, CSS and JavaScript running inside **Microsoft
Edge WebView2**. The native parts are C++20.

> **Currently supported: Windows 10 and Windows 11 only, 64-bit.**

---

## What works today

### Accounts

- Create an account and sign in right on your PC
- Passwords stored with PBKDF2-HMAC-SHA256 (120k iterations, random salt)
- Session persists across restarts
- Change password / delete account from Settings

### 4.10 build setup

- Step-by-step download page that opens the archive in your browser
- Import the downloaded ZIP — the launcher extracts and registers it
- Or import a build folder you already extracted
- Validation of the core build layout before you play
- One-click copy of the download link

### Launcher

- Dark / light theme + custom accent color
- Structured logs (viewable in-app from Settings)
- Config in `%APPDATA%\OGFNLauncher\config.json`

### Multiplayer (next phase)

- Backend OAuth login and server accounts
- Launch button unlocks once a backend session can be verified
- Auth passed to the game for multiplayer sessions

---

## Installing the launcher

### Option A — Installer (setup app)

1. Install [Inno Setup 6](https://jrsoftware.org/isdl.php) (free).
2. Re-run CMake configure — it detects Inno Setup and enables an `installer` target.
3. Build, then compile the installer:

   ```bash
   cmake --build build --config Release
   cmake --build build --target installer
   ```

4. Run `installer/out/OGFNLauncher-Setup-0.1.0.exe` — it installs the launcher
   with shortcuts like any other app.

### Option B — Portable

Build from source (below) and run `build\Release\OGFNLauncher.exe` directly.

---

## Installing the 4.10 build

The launcher walks you through this on first run (Setup & Download page):

1. **Download in your browser** — the Setup page opens the archive
   (`4.10-CL-4053532.zip`, ~27 GB) in your default browser. Save it anywhere
   you can find it.
2. **Wait for it to finish** — don't rename the file.
3. **Import** — click *Select ZIP & install* in the launcher. It extracts the
   archive next to where you saved it and registers the build.
   (Or use *pick folder* if you already extracted it.)
4. **Validation runs automatically** — the Play page shows when the build is ready.

---

## How it works

```text
┌──────────────────────────────────────────┐
│              OGFN Launcher               │
│                                          │
│  ┌─────────────────┐  ┌────────────────┐ │
│  │    WebView2     │  │    C++ Core    │ │
│  │                 │  │                │ │
│  │ HTML            │  │ Accounts       │ │
│  │ CSS             │◄►│ Build import   │ │
│  │ JavaScript      │  │ File handling  │ │
│  │                 │  │ Processes (v2) │ │
│  └─────────────────┘  └───────┬────────┘ │
└───────────────────────────────┼──────────┘
                                │ (next phase: HTTPS)
                                ▼
                       ┌─────────────────┐
                       │  OGFN Backend   │
                       │  Auth · Builds  │
                       │  News · Status  │
                       └─────────────────┘
```

The UI communicates with the C++ core through a small versioned JSON message
bridge — see [`docs/bridge-protocol.md`](docs/bridge-protocol.md).

The web UI cannot reach the internet directly (strict CSP). Everything flows
through native actions, which is exactly where backend auth and launching will
plug in.

---

## Requirements

- Windows 10 1809+ or Windows 11, 64-bit
- Microsoft Edge WebView2 Runtime (preinstalled on modern Windows)

### Building from source

- Visual Studio 2022/2026 (Desktop development with C++) or VS Build Tools
- CMake 3.20+ (bundled with Visual Studio works)

```bash
cmake -B build
cmake --build build --config Release
```

WebView2 SDK and nlohmann/json are fetched automatically at configure time —
nothing to install manually.

---

## Configuration

```text
%APPDATA%\OGFNLauncher\
├── config.json      # settings + registered build
├── accounts.json    # local accounts (PBKDF2-hashed passwords)
├── session.json     # current session
└── logs\            # launcher.log (+ 1 rotated generation)
```

---

## Project structure

```text
ogfn-launcher/
├── src/
│   ├── native/              # C++20 / Win32 / WebView2
│   └── web/                 # HTML / CSS / JavaScript (no frameworks)
├── installer/               # Inno Setup installer script
├── docs/bridge-protocol.md  # UI ⇄ native message contract
└── CMakeLists.txt
```

---

## Contributing

1. Keep the web UI framework-free.
2. Keep the native code C++20.
3. Keep the code clean and warning-free.
4. Test your changes before opening a pull request.

---

## Legal

OGFN Launcher is an unofficial community project.

It is **not affiliated with, endorsed by, or connected to Epic Games, Inc.**
Fortnite is a trademark of Epic Games, Inc.

The launcher:

- Does not include or distribute Fortnite game files.
- Links to a public archive; downloads happen in **your** browser.
- Uses builds supplied or configured by the user.
- Does not attempt to bypass DRM.

Private servers and modified Fortnite clients may be against Epic Games'
Terms of Service. You are responsible for how you use the launcher.

---

## License

OGFN Launcher is released under the **MIT License**.

---

<div align="center">

**OGFN Launcher**

*Because your launcher shouldn't be bigger than your game.*

</div>
