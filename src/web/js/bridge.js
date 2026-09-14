// bridge.js — promise-based client for the JS⇄C++ JSON bridge.
// Protocol: {id, action, payload} → {id, ok, result|error}
"use strict";

const Bridge = (() => {
  let seq = 1;
  const pending = new Map();

  window.chrome.webview.addEventListener("message", (ev) => {
    const msg = ev.data;
    if (!msg || typeof msg !== "object") return;

    // Response to a pending call.
    if (msg.id !== undefined && typeof msg.ok === "boolean") {
      const entry = pending.get(msg.id);
      if (!entry) return;
      pending.delete(msg.id);
      clearTimeout(entry.timer);
      if (msg.ok) entry.resolve(msg.result);
      else entry.reject(new Error(msg.error || "Unknown native error"));
      return;
    }

    // Unsolicited event (future phases: progress, news, status).
    if (msg.event && typeof Bridge.onEvent === "function") {
      Bridge.onEvent(msg.event, msg.data);
    }
  });

  function call(action, payload = {}) {
    return new Promise((resolve, reject) => {
      if (!window.chrome?.webview) {
        reject(new Error("Not running inside the launcher (WebView2)."));
        return;
      }
      const id = seq++;
      const timer = setTimeout(() => {
        pending.delete(id);
        reject(new Error(`${action} timed out`));
      }, 120000); // zip extraction can be slow; dialogs block the UI thread too

      pending.set(id, { resolve, reject, timer });
      window.chrome.webview.postMessage({ id, action, payload });
    });
  }

  return { call };
})();

// Small helper for HTML escaping.
function esc(s) {
  return String(s ?? "").replace(/[&<>"']/g, (c) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  })[c]);
}
