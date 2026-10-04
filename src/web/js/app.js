// app.js — launcher UI logic (vanilla JS, no frameworks).
"use strict";

// ---------------------------------------------------------------- constants

// Manual route (Setup page): opens the archive.org item page in the browser.
// In-app downloads use the native downloader with its own placeholder URL
// (src/native/download.h — swap kBuildUrl when the final link is live).
const DOWNLOAD_URL =
  "https://archive.org/details/Fortnite-4.10-CL-4053532";

// ---------------------------------------------------------------- tiny utils

const $ = (sel) => document.querySelector(sel);

function toast(msg, ms = 2600) {
  const el = $("#toast");
  el.textContent = msg;
  el.style.display = "block";
  clearTimeout(toast._t);
  toast._t = setTimeout(() => { el.style.display = "none"; }, ms);
}

async function copyText(text, what = "Link") {
  try {
    await navigator.clipboard.writeText(text);
    toast(`${what} copied to clipboard`);
  } catch {
    // Fallback for older WebView2: select-text method
    const ta = document.createElement("textarea");
    ta.value = text;
    document.body.appendChild(ta);
    ta.select();
    document.execCommand("copy");
    ta.remove();
    toast(`${what} copied to clipboard`);
  }
}

function initials(name) {
  return (name || "?").slice(0, 2).toUpperCase();
}

function fmtGB(bytes) {
  if (!bytes || bytes <= 0) return "0 GB";
  return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GB";
}

// ---------------------------------------------------------------- state

let State = {
  account: { signedIn: false, username: "" },
  config: null,
  build: null,
  download: { state: "idle", bytesNow: 0, bytesTotal: 0 },
  page: "play",
  libraryUnseen: false,   // "●" dot on the Library nav item
  pollTimer: null,
};

// ---------------------------------------------------------------- theming

function applyTheme(theme, accent) {
  document.documentElement.dataset.theme = theme === "light" ? "light" : "dark";
  if (accent && /^#[0-9a-fA-F]{6}$/.test(accent)) {
    const root = document.documentElement;
    root.style.setProperty("--accent", accent);
    // hover = 15% darker (approx), soft = 15% alpha
    const n = parseInt(accent.slice(1), 16);
    const r = (n >> 16) & 255, g = (n >> 8) & 255, b = n & 255;
    const dim = (v) => Math.max(0, Math.round(v * 0.85));
    root.style.setProperty("--accent-hover",
      `rgb(${dim(r)}, ${dim(g)}, ${dim(b)})`);
    root.style.setProperty("--accent-soft",
      `rgba(${r}, ${g}, ${b}, 0.14)`);
  }
}

// ---------------------------------------------------------------- auth

const Auth = {
  mode: "signin",

  show() {
    $("#auth-screen").classList.add("visible");
    $("#app").style.display = "none";
    setTimeout(() => $("#auth-username").focus(), 50);
  },

  hide() {
    $("#auth-screen").classList.remove("visible");
    $("#app").style.display = "flex";
  },

  setMode(mode) {
    this.mode = mode;
    const signup = mode === "signup";
    $("#tab-signin").classList.toggle("active", !signup);
    $("#tab-signup").classList.toggle("active", signup);
    $("#auth-confirm-field").style.display = signup ? "" : "none";
    $("#auth-submit").textContent = signup ? "Create account" : "Sign in";
    $("#auth-error").textContent = "";
  },

  async submit(ev) {
    ev.preventDefault();
    const username = $("#auth-username").value.trim();
    const password = $("#auth-password").value;
    const errEl = $("#auth-error");
    errEl.textContent = "";

    if (!username || !password) {
      errEl.textContent = "Enter a username and password.";
      return;
    }
    if (this.mode === "signup") {
      const confirm = $("#auth-confirm").value;
      if (password !== confirm) {
        errEl.textContent = "Passwords do not match.";
        return;
      }
    }

    const btn = $("#auth-submit");
    btn.disabled = true;
    try {
      const action = this.mode === "signup" ? "account.signUp" : "account.signIn";
      const account = await Bridge.call(action, { username, password });
      State.account = account;
      Auth.hide();
      UI.renderUser();
      toast(this.mode === "signup" ? `Welcome, ${account.username}!` : `Welcome back, ${account.username}!`);
    } catch (e) {
      errEl.textContent = e.message;
    } finally {
      btn.disabled = false;
    }
  },

  async signOut() {
    try { await Bridge.call("account.signOut"); } catch { /* ignore */ }
    State.account = { signedIn: false, username: "" };
    $("#auth-username").value = "";
    $("#auth-password").value = "";
    Auth.show();
  },
};

// ---------------------------------------------------------------- router

const Router = {
  go(page) {
    State.page = page;
    for (const p of document.querySelectorAll(".page"))
      p.classList.toggle("active", p.id === `page-${page}`);
    for (const b of document.querySelectorAll("#sidebar .nav-item"))
      b.classList.toggle("active", b.dataset.page === page);
    if (page === "library") State.libraryUnseen = false;
    UI.renderLibraryDot();
    $("#main").scrollTop = 0;
  },
};

// ---------------------------------------------------------------- UI

const UI = {
  renderUser() {
    const signedIn = State.account.signedIn;
    $("#user-name").textContent = signedIn ? State.account.username : "Not signed in";
    $("#user-avatar").textContent = signedIn ? initials(State.account.username) : "?";
    $("#user-status").textContent = signedIn ? "local account" : "";
  },

  renderLibraryDot() {
    const active = State.download &&
      (State.download.state === "downloading" || State.download.state === "installing");
    $("#library-dot").style.display =
      (active || State.libraryUnseen) ? "" : "none";
  },

  renderBuild() {
    const b = State.build || {};
    const registered = !!b.registered;
    const present = !!b.present;
    const validated = !!b.validated;
    const ready = !!b.launchReady && present && validated;
    const dl = State.download || {};
    const busy = dl.state === "downloading" || dl.state === "installing";

    // ---- play page
    const status = $("#play-status");
    const launchBtn = $("#launch-btn");
    if (!registered) {
      status.innerHTML = `<span class="badge warn">No build installed</span>
        <span class="muted" style="margin-left:10px">Get it from the <b>Library</b> tab.</span>`;
    } else if (!present) {
      status.innerHTML = `<span class="badge danger">Build folder missing</span>
        <span class="muted" style="margin-left:10px">Re-install it from the Library.</span>`;
    } else if (validated) {
      status.innerHTML = `<span class="badge ok">✔ Build ready — ${esc(b.buildId)}</span>`;
    } else {
      status.innerHTML = `<span class="badge warn">Build needs validation</span>`;
    }

    const canLaunch = ready && !busy;
    launchBtn.disabled = !canLaunch;
    launchBtn.textContent = canLaunch ? "▶ Launch" : "🔒 Launch";
    $("#launch-note").textContent = busy
      ? "Download or install in progress — launching unlocks when it finishes."
      : ready
        ? `Mode: ${(b.launchMode || "single") === "multiplayer" ? "multiplayer" : "single player"}`
        : "Install the build from the Library to unlock launching.";

    // mode toggle buttons
    const multi = (b.launchMode || "single") === "multiplayer";
    $("#mode-single").classList.toggle("primary", !multi);
    $("#mode-multi").classList.toggle("primary", multi);

    $("#build-details").innerHTML = `
      <table style="width:100%; border-collapse:collapse">
        <tr><td class="muted" style="padding:4px 0; width:180px">Build</td><td class="mono">${esc(b.buildId || "4.10-CL-4053532")}</td></tr>
        <tr><td class="muted" style="padding:4px 0">Status</td><td>${registered ? (present ? (validated ? "installed &amp; validated" : "installed (validation pending)") : "folder missing") : "not installed"}</td></tr>
        <tr><td class="muted" style="padding:4px 0">Location</td><td class="mono" style="word-break:break-all">${esc(b.path || "—")}</td></tr>
        <tr><td class="muted" style="padding:4px 0">Game executable</td><td class="mono" style="word-break:break-all">${esc(b.gameExePath || "—")}</td></tr>
      </table>`;

    // ---- setup page steps
    const stepDone = (id, done) => {
      const el = $(id);
      el.classList.toggle("done", !!done);
      el.classList.toggle("current", !done);
    };
    stepDone("#step-1", registered);
    stepDone("#step-2", registered);
    stepDone("#step-3", registered);
    stepDone("#step-4", validated);

    $("#validate-status").innerHTML = validated
      ? `<span class="ok-text">✔ Validation passed</span>`
      : (registered ? `<span class="error-text">Not validated yet</span>` : "");

    $("#import-progress").textContent = "";
  },

  renderLibrary() {
    const dl = State.download || {};
    const b = State.build || {};
    const installed = b.registered && b.present && b.validated;
    const st = dl.state || "idle";

    const actions = $("#lib-actions");
    const progress = $("#lib-progress");
    const phaseEl = $("#lib-phase");
    const errEl = $("#lib-error");
    errEl.textContent = "";

    const bar = $("#lib-bar");
    const pct = $("#lib-pct");
    const amounts = $("#lib-amounts");
    const rate = $("#lib-rate");

    const showProgress = st === "downloading" || st === "paused" || st === "installing";
    progress.style.display = showProgress ? "" : "none";
    phaseEl.style.display = st === "installing" ? "" : "none";

    let primary = null, secondary = null, note = "";

    if (st === "idle" || st === "done") {
      if (st === "done" || installed) {
        note = installed
          ? "Installed and validated. The downloaded archive is kept in %APPDATA%\\OGFNLauncher\\downloads."
          : "Download finished — waiting for install.";
        primary = { label: installed ? "Download again" : "Install now", kind: "btn", act: installed ? "start" : "install" };
        if (installed) secondary = { label: "Open Play", act: "play" };
      } else {
        note = "Not installed yet.";
        primary = { label: "⭳ Download", kind: "btn primary", act: "start" };
      }
      bar.style.width = "0%";
      pct.textContent = "";
      amounts.textContent = "";
      rate.textContent = "";
    } else if (st === "downloading" || st === "paused") {
      const total = dl.bytesTotal || 0;
      const now = dl.bytesNow || 0;
      const p = total > 0 ? Math.min(100, (now / total) * 100) : 0;
      bar.classList.remove("indeterminate");
      bar.style.width = (total > 0 ? p : 40) + "%";
      pct.textContent = total > 0 ? p.toFixed(1) + "%" : "…";
      amounts.textContent = total > 0 ? `${fmtGB(now)} / ${fmtGB(total)}` : fmtGB(now);
      rate.textContent = st === "downloading" && dl.rate
        ? (dl.rate >= 1024 ? (dl.rate / 1024).toFixed(1) + " MB/s" : dl.rate + " KB/s")
        : "";
      primary = st === "downloading"
        ? { label: "⏸ Pause", kind: "btn", act: "pause" }
        : { label: "▶ Resume", kind: "btn primary", act: "resume" };
      secondary = { label: "✕ Cancel", kind: "btn danger", act: "cancel" };
      note = st === "downloading"
        ? "Downloading… you can keep using the launcher."
        : "Paused.";
    } else if (st === "installing") {
      const phase = dl.phase || "extracting";
      const pp = (dl.phaseProgress || 0) * 100;
      if (phase === "extracting" && dl.phaseProgress > 0) {
        bar.classList.remove("indeterminate");
        bar.style.width = pp + "%";
        pct.textContent = pp.toFixed(0) + "%";
      } else {
        bar.classList.add("indeterminate");
        bar.style.width = "";
        pct.textContent = "";
      }
      amounts.textContent = "";
      rate.textContent = "";
      phaseEl.innerHTML = `<span class="spinner"></span> ${
        phase === "extracting" ? "Extracting the build (this takes a while)…"
        : phase === "importing" ? "Registering the build…"
        : "Validating…"}   <span class="faint">Phase ${phase === "extracting" ? "1" : phase === "importing" ? "2" : "3"}/3</span>`;
      primary = { label: "✕ Cancel", kind: "btn danger", act: "cancel" };
      note = "Installing — don't close the launcher.";
    } else if (st === "error") {
      errEl.textContent = dl.error || "Something went wrong.";
      primary = { label: "↻ Retry", kind: "btn primary", act: "start" };
      secondary = { label: "Clear", kind: "btn", act: "cancel" };
      bar.style.width = "0%";
      pct.textContent = "";
      amounts.textContent = "";
      rate.textContent = "";
    }

    // render buttons
    const mk = (spec, id) => {
      if (!spec) return `<button class="btn" id="${id}" type="button" style="display:none"></button>`;
      return `<button class="${spec.kind}" id="${id}" type="button" data-act="${spec.act}">${esc(spec.label)}</button>`;
    };
    actions.innerHTML = mk(primary, "lib-primary") + mk(secondary, "lib-secondary");
    const p1 = $("#lib-primary"), p2 = $("#lib-secondary");
    if (p1 && p1.dataset.act) p1.onclick = () => Library.act(p1.dataset.act);
    if (p2 && p2.dataset.act) p2.onclick = () => Library.act(p2.dataset.act);

    $("#lib-note").textContent = note;
    UI.renderLibraryDot();
  },

  renderSettings() {
    const c = State.config || {};
    $("#set-theme").value = c.theme === "light" ? "light" : "dark";
    $("#set-accent").value = /^#[0-9a-fA-F]{6}$/.test(c.accent || "") ? c.accent : "#3b82f6";

    const a = State.account;
    $("#account-card").innerHTML = a.signedIn ? `
      <div style="display:flex; align-items:center; gap:12px; margin-bottom:14px">
        <div class="avatar" style="width:38px;height:38px;border-radius:50%;background:var(--accent);color:#fff;display:grid;place-items:center;font-weight:700">${esc(initials(a.username))}</div>
        <div>
          <div style="font-weight:700">${esc(a.username)}</div>
          <div class="faint">local account · stored on this PC</div>
        </div>
      </div>
      <div style="display:flex; gap:10px; flex-wrap:wrap">
        <button class="btn" id="btn-chpass" type="button">Change password…</button>
        <button class="btn danger" id="btn-delacc" type="button">Delete account…</button>
      </div>
    ` : `<span class="muted">Not signed in.</span>`;

    const v = (State.config && State.config.launcher && State.config.launcher.version) || "0.1.0";
    $("#about-card").innerHTML = `
      <div class="mono" style="margin-bottom:6px">OGFN Launcher v${esc(v)}</div>
      <div class="faint">Targets Fortnite build 4.10 (CL 4053532) · Season 4 only.</div>
      <div class="faint">Single-player launching is available now; multiplayer
      unlocks with the server phase.</div>
    `;

    const ch = $("#btn-chpass");
    if (ch) ch.onclick = UI.changePasswordFlow;
    const del = $("#btn-delacc");
    if (del) del.onclick = UI.deleteAccountFlow;
  },

  async changePasswordFlow() {
    const current = prompt("Current password:");
    if (current === null) return;
    const next = prompt("New password (min 8 chars):");
    if (next === null) return;
    try {
      await Bridge.call("account.changePassword", { currentPassword: current, newPassword: next });
      toast("Password changed");
    } catch (e) {
      alert(e.message);
    }
  },

  async deleteAccountFlow() {
    if (!confirm("Delete your account? This cannot be undone.")) return;
    const pw = prompt("Confirm your password to delete the account:");
    if (pw === null) return;
    try {
      await Bridge.call("account.delete", { password: pw });
      State.account = { signedIn: false, username: "" };
      Auth.show();
      toast("Account deleted");
    } catch (e) {
      alert(e.message);
    }
  },
};

// ---------------------------------------------------------------- library

const Library = {
  async act(action) {
    try {
      if (action === "play") { Router.go("play"); return; }
      if (action === "start") {
        State.download = await Bridge.call("download.start");
        toast("Download started");
      } else if (action === "pause") {
        State.download = await Bridge.call("download.pause");
      } else if (action === "resume") {
        State.download = await Bridge.call("download.resume");
        toast("Download resumed");
      } else if (action === "cancel") {
        State.download = await Bridge.call("download.cancel");
        toast("Download cancelled");
      } else if (action === "install") {
        State.download = await Bridge.call("download.install");
        toast("Installing…");
      }
    } catch (e) {
      toast(e.message);
    }
    UI.renderLibrary();
    this.ensurePolling();
  },

  ensurePolling() {
    const st = State.download && State.download.state;
    const active = st === "downloading" || st === "installing";
    if (active && !State.pollTimer) {
      State.pollTimer = setInterval(async () => {
        try {
          State.download = await Bridge.call("download.status");
          UI.renderLibrary();
          const s = State.download.state;
          if (s !== "downloading" && s !== "installing") {
            clearInterval(State.pollTimer);
            State.pollTimer = null;
          }
        } catch { /* bridge hiccup — keep polling */ }
      }, 1500);
    }
  },
};

// ---- bridge events (pushed from native) ------------------------------------

Bridge.onEvent = async (event, data) => {
  if (event === "download.progress") {
    if (State.page === "library" ||
        (data && (data.state === "downloading" || data.state === "installing"))) {
      State.download = { ...State.download, ...data };
      if (State.page === "library") UI.renderLibrary();
      UI.renderBuild();
    }
  } else if (event === "download.state" || event === "download.error") {
    const prev = State.download ? State.download.state : "idle";
    State.download = { ...State.download, ...data };
    UI.renderLibrary();
    UI.renderBuild();

    if (data.state === "done" && prev !== "done") {
      State.libraryUnseen = State.page !== "library";
      toast("✔ Fortnite — Season 4 downloaded and installed. Ready to play!", 5000);
      await refreshState();
    } else if (data.state === "error" && prev !== "error") {
      toast("Download problem: " + (data.error || "unknown error"), 5000);
    } else if (data.state === "paused" && prev === "downloading") {
      toast("Download paused");
    } else if (data.state === "downloading" && prev === "paused") {
      toast("Download resumed");
    } else if (data.state === "idle" && prev !== "idle") {
      toast("Download cancelled");
    }
    Library.ensurePolling();
  }
};

// ---------------------------------------------------------------- setup flow

const Setup = {
  async importZip() {
    const note = $("#import-progress");
    try {
      note.textContent = "Choose the downloaded ZIP…";
      const pick = await Bridge.call("dialog.pickZip");
      if (!pick.path) { note.textContent = ""; return; }

      note.innerHTML = `<span class="spinner"></span> Extracting — this can take several minutes…`;
      const build = await Bridge.call("build.importFromZip", { zipPath: pick.path });
      State.build = build;
      note.textContent = "";
      toast("Build imported and validated ✔");
      await refreshState();
      Router.go("play");
    } catch (e) {
      note.textContent = "";
      alert(`Import failed:\n\n${e.message}`);
    }
  },

  async importFolder() {
    try {
      const pick = await Bridge.call("dialog.pickFolder");
      if (!pick.path) return;
      const build = await Bridge.call("build.importFromFolder", { folderPath: pick.path });
      State.build = build;
      toast("Build imported ✔");
      await refreshState();
      Router.go("play");
    } catch (e) {
      alert(`Import failed:\n\n${e.message}`);
    }
  },

  async validate() {
    const el = $("#validate-status");
    el.innerHTML = `<span class="spinner"></span>`;
    try {
      const r = await Bridge.call("build.validate");
      if (r.ok) {
        el.innerHTML = `<span class="ok-text">✔ ${r.checksPassed}/${r.checksTotal} checks passed</span>`;
      } else {
        el.innerHTML = `<span class="error-text">${esc(r.error || "Validation failed")}</span>`;
      }
      const st = await Bridge.call("state.get");
      State.build = st.build;
      UI.renderBuild();
    } catch (e) {
      el.innerHTML = `<span class="error-text">${esc(e.message)}</span>`;
    }
  },
};

// ---------------------------------------------------------------- boot

async function refreshState() {
  const st = await Bridge.call("state.get");
  State.account = st.account;
  State.config = st.config;
  State.build = st.build;
  State.download = st.download || State.download;
  applyTheme(State.config.theme, State.config.accent);
  UI.renderUser();
  UI.renderBuild();
  UI.renderLibrary();
  UI.renderSettings();
  Library.ensurePolling();
}

function wire() {
  // auth tabs + submit
  $("#tab-signin").addEventListener("click", () => Auth.setMode("signin"));
  $("#tab-signup").addEventListener("click", () => Auth.setMode("signup"));
  $("#auth-form").addEventListener("submit", (e) => Auth.submit(e));

  $("#signout-btn").addEventListener("click", () => Auth.signOut());

  // router
  for (const b of document.querySelectorAll("#sidebar .nav-item")) {
    b.addEventListener("click", () => Router.go(b.dataset.page));
  }

  // setup page
  $("#open-download-page").addEventListener("click", async () => {
    try { await Bridge.call("shell.openInBrowser", { url: DOWNLOAD_URL }); }
    catch (e) { toast(e.message); }
  });
  $("#copy-download-link").addEventListener("click", () => copyText(DOWNLOAD_URL, "Download link"));
  $("#copy-link-2").addEventListener("click", () => copyText(DOWNLOAD_URL, "Download link"));
  $("#download-url").textContent = DOWNLOAD_URL;

  $("#import-zip-btn").addEventListener("click", () => Setup.importZip());
  $("#import-folder-btn").addEventListener("click", () => Setup.importFolder());
  $("#validate-btn").addEventListener("click", () => Setup.validate());

  // play page — launch + mode
  $("#launch-btn").addEventListener("click", async () => {
    const b = State.build || {};
    const mode = b.launchMode || "single";
    try {
      $("#launch-btn").disabled = true;
      const r = await Bridge.call("game.launch", { mode });
      toast(r.mode === "multiplayer"
        ? "Game launched (multiplayer)"
        : "Game launched — have fun!", 4000);
    } catch (e) {
      toast(e.message, 4500);
    } finally {
      UI.renderBuild();
    }
  });
  $("#mode-single").addEventListener("click", async () => {
    try {
      State.config = await Bridge.call("config.patch", { game: { mode: "single" } });
    } catch { /* non-fatal */ }
    if (State.build) State.build.launchMode = "single";
    UI.renderBuild();
  });
  $("#mode-multi").addEventListener("click", () => {
    toast("Multiplayer unlocks with the server phase — single player works now.", 4200);
  });

  // settings
  $("#set-theme").addEventListener("change", async (e) => {
    State.config = await Bridge.call("config.patch", { theme: e.target.value });
    applyTheme(State.config.theme, State.config.accent);
  });
  $("#set-accent").addEventListener("change", async (e) => {
    State.config = await Bridge.call("config.patch", { accent: e.target.value });
    applyTheme(State.config.theme, State.config.accent);
  });
  $("#open-data-folder").addEventListener("click", async () => {
    try { await Bridge.call("shell.openDataFolder"); } catch (e) { toast(e.message); }
  });
  $("#view-logs-btn").addEventListener("click", async () => {
    try {
      const r = await Bridge.call("logs.get");
      showLogOverlay(r.logs || "(empty)");
    } catch (e) { toast(e.message); }
  });
}

function showLogOverlay(text) {
  const existing = document.getElementById("log-overlay");
  if (existing) existing.remove();
  const wrap = document.createElement("div");
  wrap.id = "log-overlay";
  wrap.style.cssText = "position:fixed;inset:0;background:rgba(0,0,0,.55);z-index:98;display:grid;place-items:center";
  const box = document.createElement("div");
  box.style.cssText = "width:760px;max-width:92vw;height:70vh;background:var(--bg-card);border:1px solid var(--border);border-radius:12px;display:flex;flex-direction:column;overflow:hidden";
  const head = document.createElement("div");
  head.style.cssText = "display:flex;align-items:center;justify-content:space-between;padding:12px 16px;border-bottom:1px solid var(--border);font-weight:700";
  head.innerHTML = `<span>Launcher logs</span>`;
  const close = document.createElement("button");
  close.className = "btn small";
  close.textContent = "Close";
  close.onclick = () => wrap.remove();
  head.appendChild(close);
  const pre = document.createElement("pre");
  pre.className = "mono";
  pre.style.cssText = "margin:0;padding:14px 16px;overflow:auto;flex:1;font-size:12px;white-space:pre-wrap;user-select:text";
  pre.textContent = text;
  box.appendChild(head);
  box.appendChild(pre);
  wrap.appendChild(box);
  document.body.appendChild(wrap);
}

(async function boot() {
  wire();

  try {
    await refreshState();
  } catch (e) {
    // Bridge not available (e.g. opened in a plain browser) — show auth anyway.
    console.warn("state.get failed:", e);
  }

  if (State.account && State.account.signedIn) {
    Auth.hide();
  } else {
    Auth.show();
  }
  Router.go("play");
})();
