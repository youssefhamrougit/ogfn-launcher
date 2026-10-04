# Bridge Protocol — OGFN Launcher

Versioned JSON protocol between the web UI (JavaScript) and the native core (C++).
Transport: WebView2 `postMessage` / `WebMessageReceived`.

## Request (UI → native)

```json
{ "id": 1, "action": "account.signIn", "payload": { "username": "ace", "password": "hunter2!" } }
```

- `id` — any JSON scalar; echoed back.
- `action` — one of the actions below.
- `payload` — action-specific object (may be `{}`).

## Response (native → UI)

Success:

```json
{ "id": 1, "ok": true, "result": { "signedIn": true, "username": "ace" } }
```

Failure:

```json
{ "id": 1, "ok": false, "error": "Incorrect password." }
```

Unsolicited events (native → UI):

```json
{ "event": "download.progress", "data": { "state": "downloading", "bytesNow": 42, "bytesTotal": 100, "percent": 42.0, "rate": 2048 } }
{ "event": "download.state",     "data": { } }   // full download status snapshot
{ "event": "download.error",     "data": { } }   // status snapshot + error
```

Events are raised by background workers (downloader). They are queued on the
UI thread and delivered via `PostWebMessageAsJson`.

## Actions (current)

| Action | Payload | Result |
|---|---|---|
| `ping` | — | `{pong, version}` |
| `state.get` | — | `{version, account, config, build, download}` (build includes `launchReady`, `launchMode`; single-player launching is live, multiplayer waits for the server phase) |
| `config.patch` | deep-merge object, e.g. `{theme:"light"}` | full config |
| `account.signUp` | `{username, password}` | `{signedIn, username}` |
| `account.signIn` | `{username, password}` | `{signedIn, username}` |
| `account.signOut` | — | `{signedIn, username}` |
| `account.changePassword` | `{currentPassword, newPassword}` | `{changed}` |
| `account.delete` | `{password}` | `{signedIn:false}` |
| `download.status` | — | download status object |
| `download.start` | — | download status object (starts/resumes the build download) |
| `download.pause` | — | download status object |
| `download.resume` | — | download status object |
| `download.cancel` | — | download status object (deletes the partial file) |
| `download.install` | — | download status object (extract → import → validate from the already-downloaded zip) |
| `game.launch` | `{mode: "single"\|"multiplayer"}` | `{launched, pid, exePath, mode}` — multiplayer is rejected until the server phase |
| `build.importFromZip` | `{zipPath}` | build status object |
| `build.importFromFolder` | `{folderPath}` | build status object |
| `build.validate` | — | `{ok, error, checksPassed, checksTotal}` |
| `build.remove` | — | build status object |
| `dialog.pickZip` | — | `{path}` (`""` if cancelled) |
| `dialog.pickZipSave` | — | `{path}` |
| `dialog.pickFolder` | — | `{path}` |
| `shell.openInBrowser` | `{url}` (http/https only) | `{opened}` |
| `shell.openDataFolder` | — | `{opened}` |
| `logs.get` | — | `{logs}` |
| `logs.clearData` | — | `{cleared}` |

### Download status object

```json
{
  "state": "idle | downloading | paused | installing | done | error",
  "url": "https://…",
  "error": "",
  "bytesNow": 123456789,
  "bytesTotal": 28000000000,
  "zipPath": "C:/…/downloads/4.10-CL-4053532.zip",
  "fileName": "4.10-CL-4053532.zip",
  "phase": "extracting | importing | validating",
  "phaseProgress": 0.42
}
```

`phase`/`phaseProgress` appear only while installing. Downloads resume via
HTTP `Range` from the partial file; a `completed.flag` beside the zip marks a
finished-but-not-installed download so install can resume after a restart.

## Rules

1. Errors are always strings, safe to show the user directly.
2. Native never trusts the UI: passwords stay in native memory only as long as needed;
   stored PBKDF2-hashed (120k iterations, random 16-byte salt).
3. The UI cannot reach the internet directly (`connect-src 'none'` CSP); all I/O flows
   through native actions. This is what the downloader/auth of later phases will hook into.
