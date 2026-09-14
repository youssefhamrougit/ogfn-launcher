// bridge.h — versioned JSON message protocol between web UI and native core.
//
// UI -> native  : { "id": <int>, "action": "<name>", "payload": {...}? }
// native -> UI  : { "id": <int>, "ok": true, "result": {...} }
//               | { "id": <int>, "ok": false, "error": "<message>" }
// native -> UI  : { "event": "<name>", "data": {...} }   (unsolicited)
#pragma once

#include <nlohmann/json.hpp>

namespace bridge {

// Dispatch one request and produce a response. Never throws.
nlohmann::json Handle(const nlohmann::json& request);

} // namespace bridge
