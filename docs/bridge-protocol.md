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

Unsolicited events (future phases):

```json
{ "event": "download.progress", "data": { "percent": 42 } }
```

## Actions (current)

| Action | Payload | Result |
|---|---|---|
| `ping` | — | `{pong, version}` |
| `state.get` | — | `{version, account, config, build}` (build includes `launchBlocked: true` until the multiplayer phase) |
| `config.patch` | deep-merge object, e.g. `{theme:"light"}` | full config |
| `account.signUp` | `{username, password}` | `{signedIn, username}` |
| `account.signIn` | `{username, password}` | `{signedIn, username}` |
| `account.signOut` | — | `{signedIn, username}` |
| `account.changePassword` | `{currentPassword, newPassword}` | `{changed}` |
| `account.delete` | `{password}` | `{signedIn:false}` |
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

## Rules

1. Errors are always strings, safe to show the user directly.
2. Native never trusts the UI: passwords stay in native memory only as long as needed;
   stored PBKDF2-hashed (120k iterations, random 16-byte salt).
3. The UI cannot reach the internet directly (`connect-src 'none'` CSP); all I/O flows
   through native actions. This is what the downloader/auth of later phases will hook into.
