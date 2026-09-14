<div align="center">

# OGFN Launcher

**A simple, lightweight launcher for OGFN (OG Fortnite) private servers.**

[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)](#requirements)
[![License](https://img.shields.io/github/license/OWNER/REPO)](LICENSE)

Built with **C++ and vanilla HTML/CSS/JS**.  
No Electron. No big frameworks. Just a small native launcher.

</div>

---

## What is OGFN Launcher?

OGFN Launcher is an open-source launcher for **OG Fortnite private servers**.

The goal is simple: give you one place to manage your Fortnite builds, connect to a backend, and launch the game without having to manually deal with a bunch of files and commands.

The launcher is **backend-agnostic**, meaning it isn't made for one specific private server. You can configure the backend you want to use.

The interface is built with regular HTML, CSS and JavaScript and runs inside **Microsoft Edge WebView2**. The native parts of the launcher are handled by C++.

> **Currently supported: Windows 10 and Windows 11 only.**
>
> **Targets Fortnite build 4.10 only.** The launcher manages and launches a single build (4.10), with multiplayer support landing soon.

---

## Features

### Build setup (4.10)

- Import your Fortnite 4.10 build
- Validate the build files before launching
- Launch settings for the build
- Download the 4.10 build from a configured archive

### Downloads

- Resumable downloads
- Chunked downloads for large builds
- Automatic retries
- SHA-256 verification
- Manifest-based build information (single 4.10 manifest)

Large builds can be tens of gigabytes, so the launcher is designed to handle interrupted downloads without starting over.

### Launching

- Launch the 4.10 build with one click
- Backend auth passed to the game for multiplayer (coming soon)
- Basic process monitoring

### Backend

- Configure your own backend URL
- Epic-style authentication
- Token verification
- Backend status information
- News feed support

The launcher itself doesn't depend on one specific OGFN backend.

### Updates

- Launcher self-updater
- Stable and beta release channels
- Version checking

### Quality of life

- Settings page
- Build notes
- Structured logs
- Light/dark themes
- Custom accent colors
- Backend news and status

---

## How it works

The launcher is split into two main parts:

```text
┌──────────────────────────────────────────┐
│              OGFN Launcher               │
│                                          │
│  ┌─────────────────┐  ┌────────────────┐ │
│  │    WebView2     │  │    C++ Core    │ │
│  │                 │  │                │ │
│  │ HTML             │  │ Downloads      │ │
│  │ CSS              │  │ File handling  │ │
│  │ JavaScript       │◄►│ Processes      │ │
│  │                 │  │ Updates        │ │
│  └─────────────────┘  └───────┬────────┘ │
│                               │          │
└───────────────────────────────┼──────────┘
                                │ HTTPS
                                ▼
                       ┌─────────────────┐
                       │  OGFN Backend   │
                       │                 │
                       │ Auth            │
                       │ News            │
                       │ Builds          │
                       │ Status          │
                       └─────────────────┘
```

### Web UI

The UI lives in `src/web`.

It uses:

- HTML
- CSS
- JavaScript

There are **no frontend frameworks**.

### Native core

The native code lives in `src/native`.

It handles things that need access to Windows, such as:

- Downloading files
- Checking file hashes
- Starting Fortnite
- Monitoring processes
- Saving configuration
- Updating the launcher

The UI communicates with the C++ core through a small JSON message bridge.

---

## Native modules

| File | What it does |
|---|---|
| `main.cpp` | Creates the Windows window and WebView2 environment |
| `bridge.cpp` | Handles communication between JavaScript and C++ |
| `downloads.cpp` | Downloads, resumes and verifies files |
| `crypto.cpp` | SHA-256 verification |
| `process.cpp` | Starts and monitors Fortnite processes |
| `config.cpp` | Saves launcher settings |
| `updater.cpp` | Checks for and installs launcher updates |

---

## Requirements

OGFN Launcher currently supports **Windows only**.

### Supported

- Windows 10 1809 or newer
- Windows 11
- 64-bit Windows

### For building from source

You'll need:

- Visual Studio 2022
- Desktop development with C++
- MSVC v143
- CMake 3.20+
- WebView2 SDK

WebView2 is already included with modern versions of Windows 10 and Windows 11. The installer can also install it if needed.

---

## Installing

Download the latest release from the GitHub Releases page.

Run the installer and start OGFN Launcher.

On the first launch:

1. Enter your backend URL.
2. Log in.
3. Import your Fortnite 4.10 build or download it from your configured archive.
4. Launch the build.

That's it.

---

## Building from source

Clone the repository:

```bash
git clone https://github.com/OWNER/REPO.git
cd REPO
```

Configure the project:

```bash
cmake -B build
```

Build it:

```bash
cmake --build build --config Release
```

For development, you can run a Debug build and edit the web files without rebuilding the whole UI every time.

---

## Configuration

The launcher stores its configuration here:

```text
%APPDATA%\OGFNLauncher\config.json
```

Example:

```json
{
  "backendUrl": "https://your-ogfn-backend.example.com",
  "releaseChannel": "stable",
  "defaultBuildPath": "D:/Games/Fortnite Builds",
  "launchArguments": {},
  "keepLogs": true
}
```

You normally won't need to edit this file manually.

---

## Backend API

The launcher is designed to work with different OGFN backends.

A backend can provide things like authentication, available builds, news and server status.

The current API contract is:

| Method | Endpoint | Purpose |
|---|---|---|
| `POST` | `/account/api/oauth/token` | Login |
| `GET` | `/account/api/oauth/verify` | Check authentication |
| `GET` | `/launcher/api/news` | Launcher news |
| `GET` | `/launcher/api/manifest` | Available builds |
| `GET` | `/launcher/api/status` | Backend status |

The launcher only handles the parts it needs. Once Fortnite starts, the game communicates with the backend normally.

---

## Roadmap

### v1.0.0 — Core

- [x] Backend login
- [x] Configurable backend URL
- [x] Import local builds
- [x] Resumable downloads
- [x] SHA-256 verification
- [x] Launch builds on Windows
- [ ] Multiplayer-ready launches (4.10)

### v1.1.0 — Quality of life

- [x] Launcher self-updater
- [x] Stable/beta channels
- [x] Settings
- [x] Logs
- [x] Per-build launch arguments
- [x] Multiple clients

### v1.2.0 — Current

- [x] Injection manager
- [x] DLL library and presets
- [x] News feed
- [x] Backend status
- [x] Light/dark themes
- [x] Custom accent colors
- [x] Signed installer
- [x] Verified releases

---

## Project structure

```text
ogfn-launcher/
│
├── src/
│   ├── native/              # C++20 / Win32 / WebView2
│   │
│   └── web/                 # HTML / CSS / JavaScript
│       ├── index.html
│       ├── css/
│       └── js/
│
├── installer/               # Inno Setup installer
├── backend/                 # Reference backend
├── docs/                    # Documentation and screenshots
├── .github/
│   └── workflows/           # CI and releases
│
└── CMakeLists.txt
```

---

## Contributing

Contributions are welcome.

A few simple rules:

1. Keep the web UI framework-free.
2. Keep the native code C++20.
3. Keep the code clean and warning-free.
4. Test your changes before opening a pull request.
5. For larger changes, open an issue first so we can discuss them.

---

## Legal

OGFN Launcher is an unofficial community project.

It is **not affiliated with, endorsed by, or connected to Epic Games, Inc.**

Fortnite is a trademark of Epic Games, Inc.

The launcher:

- Does not include Fortnite game files.
- Does not distribute Epic Games assets.
- Does not provide Fortnite builds itself.
- Uses builds supplied or configured by the user.
- Does not attempt to bypass DRM.

Private servers and modified Fortnite clients may be against Epic Games' Terms of Service. You are responsible for how you use the launcher.

---

## License

OGFN Launcher is released under the **MIT License**.

Third-party libraries and components remain under their respective licenses.

---

## Credits

This project wouldn't exist without the work of the OGFN community.

Thanks to the developers and contributors behind projects such as:

- LawinServer
- Neonite
- Reboot Launcher
- Fortnite build archivists
- Everyone working on the OGFN ecosystem

---

<div align="center">

**OGFN Launcher**

*Because your launcher shouldn't be bigger than your game.*

</div>