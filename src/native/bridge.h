// bridge.h — versioned JSON message protocol between web UI and native core.
//
// UI -> native  : { "id": <int>, "action": "<name>", "payload": {...}? }
// native -> UI  : { "id": <int>, "ok": true, "result": {...} }
//               | { "id": <int>, "ok": false, "error": "<message>" }
// native -> UI  : { "event": "<name>", "data": {...} }   (unsolicited)
#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace bridge {

// Push an unsolicited event to the UI: {"event": name, "data": data}.
// Set once by main.cpp after WebView2 is ready (SetEventPoster). No-op before.
using EventPoster = std::function<void(const std::string& event,
                                       const nlohmann::json& data)>;
void SetEventPoster(EventPoster poster);
void PostEvent(const std::string& event, const nlohmann::json& data);

// Dispatch one request and produce a response. Never throws.
nlohmann::json Handle(const nlohmann::json& request);

} // namespace bridge
