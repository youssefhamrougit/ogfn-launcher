// app.js — launcher UI logic (vanilla JS, no frameworks).
"use strict";

// ---------------------------------------------------------------- constants

const DOWNLOAD_URL =
  "https://dn720703.ca.archive.org/0/items/Fortnite-4.10-CL-4053532.zip/4.10-CL-4053532.zip";

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

// ---------------------------------------------------------------- state

let State = {
  account: { signedIn: false, username: "" },
  config: null,
  build: null,
  page: "play",
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

  renderBuild() {
    const b = State.build || {};
    const registered = !!b.registered;
    const present = !!b.present;
    const validated = !!b.validated;

    // ---- play page
    const status = $("#play-status");
    const launchBtn = $("#launch-btn");
    if (!registered) {
      status.innerHTML = `<span class="badge warn">No build imported</span>
        <span class="muted" style="margin-left:10px">Go to <b>Setup &amp; Download</b> to get Season 4.</span>`;
      launchBtn.disabled = true;
      launchBtn.textContent = "🔒 Launch";
    } else if (!present) {
      status.innerHTML = `<span class="badge danger">Build folder missing</span>
        <span class="muted" style="margin-left:10px">Re-import it from Setup &amp; Download.</span>`;
      launchBtn.disabled = true;
      launchBtn.textContent = "🔒 Launch";
    } else if (validated) {
      status.innerHTML = `<span class="badge ok">✔ Build ready — ${esc(b.buildId)}</span>
        <span class="muted" style="margin-left:10px">Waiting for multiplayer phase to unlock launching.</span>`;
      launchBtn.disabled = true;
      launchBtn.textContent = "🔒 Launch";
    } else {
      status.innerHTML = `<span class="badge warn">Build needs validation</span>`;
      launchBtn.disabled = true;
      launchBtn.textContent = "🔒 Launch";
    }

    $("#build-details").innerHTML = `
      <table style="width:100%; border-collapse:collapse">
        <tr><td class="muted" style="padding:4px 0; width:180px">Build</td><td class="mono">${esc(b.buildId || "4.10-CL-4053532")}</td></tr>
        <tr><td class="muted" style="padding:4px 0">Status</td><td>${registered ? (present ? (validated ? "imported &amp; validated" : "imported (validation pending)") : "folder missing") : "not imported"}</td></tr>
        <tr><td class="muted" style="padding:4px 0">Location</td><td class="mono" style="word-break:break-all">${esc(b.path || "—")}</td></tr>
        <tr><td class="muted" style="padding:4px 0">Game executable</td><td class="mono" style="word-break:break-all">${esc(b.gameExePath || "—")}</td></tr>
      </table>`;

    // ---- setup page steps
    const stepDone = (id, done) => {
      const el = $(id);
      el.classList.toggle("done", !!done);
      el.classList.toggle("current", !done);
    };
    stepDone("#step-1", registered);      // they can move on once a build exists
    stepDone("#step-2", registered);
    stepDone("#step-3", registered);
    stepDone("#step-4", validated);

    $("#validate-status").innerHTML = validated
      ? `<span class="ok-text">✔ Validation passed</span>`
      : (registered ? `<span class="error-text">Not validated yet</span>` : "");

    $("#import-progress").textContent = "";
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
      <div class="faint">Multiplayer launching is developed in the next phase —
      this release prepares your build so it's ready the day servers arrive.</div>
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
      UI.renderBuild();
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
      UI.renderBuild();
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
  applyTheme(State.config.theme, State.config.accent);
  UI.renderUser();
  UI.renderBuild();
  UI.renderSettings();
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

  // launch button (locked) — explain why
  $("#launch-btn").addEventListener("click", () => {
    toast("Launching unlocks with the multiplayer/server phase.");
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
