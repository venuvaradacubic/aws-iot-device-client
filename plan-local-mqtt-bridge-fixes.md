# Local MQTT Bridge Remediation & Completion Plan

## 1. Purpose & Scope
This plan enumerates all defects, gaps, and divergences between the current `local-mqtt-bridge` implementation and the original design (`plan.md`), and defines the corrective actions required to deliver a production‑quality, fully spec‑compliant feature.

Scope includes: code repair, correctness, parity with configuration contract, resilience (offline/throttling), metrics, test coverage, documentation, and build portability (Linux only).

---
## 2. Executive Summary of Issues
| Category | Key Problems |
|----------|--------------|
| Structural | Corrupted `LocalMqttBridgeFeature.cpp` header and duplicated lambdas; up-route wildcard variables not substituted; missing metrics module. |
| Spec Deviations | Throttling not implemented; metrics not implemented; heartbeat dedupe hardcoded (not config/route-driven); QoS2 handling missing; offline queue semantics incomplete; no down-direction tagging. |
| Config Fidelity | Some config fields (`throttleSeconds`, `metrics.*`, `dedupeHeartbeat`, `useTLS`) ignored silently. |
| Robustness | No AWS offline detection/retry; minimal reconnection backoff for local broker; no TLS usage path. |
| Testing | Zero dedicated unit tests for bridge components. |
| Documentation | No README or CONFIG.md additions; metrics & throttling undocumented. |
| Style/Consistency | Mixed header guard styles; no notifier error propagation; hard-coded heartbeat regex contrary to plan. |

---
## 3. Objectives
1. Restore syntactic integrity of corrupted source file(s).
2. Achieve full functional parity with the original plan (routing, wildcard captures, variable expansion, throttling, loop prevention, offline buffering, metrics publishing, QoS handling).
3. Eliminate silent no-op config fields—each must act, warn, or error.
4. Add deterministic, test‑covered logic for routing & throttling.
5. Provide observability (metrics counters + periodic publish) and instrumentation for debugging.
6. Ensure safe degradation (QoS2 downgrade, loop guard, heartbeat dedupe optional).
7. Deliver comprehensive unit tests and minimal integration tests.
8. Align style with existing features (guards, logging, feature lifecycle patterns).
9. Provide cross-platform build resilience (Windows lacks pkg-config).

---
## 4. Detailed Gap → Action Matrix
| Gap | Impact | Action | Files | Acceptance Criteria |
|-----|--------|--------|-------|---------------------|
| Corrupted `LocalMqttBridgeFeature.cpp` prologue & duplicated lambdas | Compile failure / undefined behavior | Rebuild clean file intro, remove duplicates, correct lambda capture variable (`receivedTopic`→`receivedOnTopic`) | `LocalMqttBridgeFeature.cpp` | Builds clean; no duplicate lambdas; correct AWS subscription handler |
| Up-route wildcard vars (`+`) not applied to AWS topic | Publishes invalid topics (literal `${match1}`) | Extend `RouteMatcher` to produce capture map for up routes; add `matchUpWithVariables` returning captures; reuse compilation logic with flag | `RouteMatcher.{h,cpp}`, `LocalMqttBridgeFeature.cpp` | Local topic `metrics/gate/temp` → AWS `devices/thing/metrics/gate/temp` |
| Missing throttling (`throttleSeconds`) | Possible flood to AWS IoT; spec non-compliant | Implement `shouldThrottle()` with per-topic state (time + last payload) and send-latest-after-window flush | `LocalMqttBridgeFeature.{h,cpp}` | Unit test shows suppression within window and final send after expiry |
| Metrics absent | No observability | Add `Metrics.h/.cpp` with counters; integrate increments; thread to publish JSON to `metrics.awsTopic`; honor `metrics.enabled` | New `Metrics.h/.cpp`, `LocalMqttBridgeFeature.cpp` | Metrics JSON published at configured interval; counters reset or accumulate per design |
| Hard-coded heartbeat regex & ignoring `dedupeHeartbeat` | Unintended drops; spec violation | Remove regex logic from `Queue`; pass `dedupeHeartbeat` flag; treat heartbeat only if enabled AND route/topic contains “heartbeat” OR future explicit flag | `Queue.{h,cpp}`, `LocalMqttBridgeFeature.cpp` | When `dedupeHeartbeat=false`, all messages enqueued; when true, sequential identical heartbeat suppressed |
| QoS2 handling missing | Unexpected behavior if QoS2 arrives | Detect QoS 2 inbound/outbound; log warning; downgrade to QoS1 or drop per plan; increment `droppedQos2` counter | `LocalMqttBridgeFeature.cpp` | QoS2 message increments counter & not forwarded as QoS2 |
| Offline queue semantics vague | Message loss during AWS disconnect | Check `connection->GetIsConnected()`; if false, enqueue; periodic drain attempt when reconnected | `LocalMqttBridgeFeature.cpp` | Messages published after reconnection; test using simulated disconnect hook |
| TLS local broker not used | Security flag ignored | If `useTLS=true`, call mosquitto TLS APIs (`mosquitto_tls_set`, allow CA path config extension) else log warning until full path implemented | `LocalClient.{h,cpp}` | Logs confirm TLS initialization path or explicit warning |
| Direction tagging only “up” | Potential loop ambiguity | Optionally tag both directions; skip re-tag if already tagged; or strip `_bridge` before delivering local if configured; implement symmetric tagging | `PayloadTagger.cpp`, `LocalMqttBridgeFeature.cpp` | Downlink JSON contains `"_bridge":"down"` unless already tagged |
| Two queues vs single + direction | Acceptable divergence; complexity | Keep two (low risk) but document rationale; ensure consistent metrics | README / comments | Documented trade-off |
| No test suite | Risk of regression | Add tests: RouteMatcher (up/down regex + expansion), Throttling, LoopGuard, Tagger idempotency, Queue dedupe, Feature integration smoke (if MQTT mocked) | `test/local-mqtt-bridge/*.cpp`, CMake test list | All new tests build & pass locally |
| Missing docs | Operator confusion | Add feature README, update `docs/CONFIG.md` and root README feature list | New/updated docs | Docs list all config keys and behaviors |
| Mixed header style | Inconsistency | Replace `#pragma once` with guards matching repo scheme | All new/modified headers | No `#pragma once` in bridge headers |
| No Windows fallback for libmosquitto discover | Build failure on Windows | Add `find_library` & `find_path` fallback when `PkgConfig` not found or platform WIN32; allow optional static link flag | `CMakeLists.txt` | Windows build finds mosquitto or emits clear fatal message |
| No notifier error propagation | Silent failures | On subscription or publish failures invoke `baseNotifier->onError(...)` with meaningful code | `LocalMqttBridgeFeature.cpp` | Errors appear in notifier callbacks |
| Missing metrics counters definitions | Observability gap | Implement counters per plan + serialization (`toJson`) | `Metrics.{h,cpp}` | JSON includes all counters |

---
## 5. Phased Implementation Strategy
| Phase | Goal | Contents | Exit Criteria |
|-------|------|----------|---------------|
| 1 | Stabilize Build | Repair corruption, fix basic routing bug, add unit tests for current behavior | Clean build; tests compile & run |
| 2 | Core Spec Completion | Wildcard vars on up routes, throttling, QoS2 handling, heartbeat config respect | Feature parity for routing/throttle/QoS |
| 3 | Resilience & Metrics | Offline queue drain logic, metrics counters + publisher thread | Metrics messages visible; offline replay works |
| 4 | TLS & Config Fidelity | Implement/guard TLS path; warnings for not-yet-supported advanced TLS options | Secure connection path or explicit warning |
| 5 | Documentation & Polish | README/config docs, style harmonization, notifier integration | Docs merged; lint/style ok |
| 6 | Final QA | Stress tests, memory scan (valgrind on Linux), race checks | No leaks; no data races in queue/loop guard |

---
## 6. Testing Plan
### 6.1 Unit Tests
| Test | Focus | Notes |
|------|-------|-------|
| `TestRouteMatcher.cpp` | Down wildcard (+/#) capture; up wildcard expansion | Include edge empty segments, tail capture |
| `TestLoopGuard.cpp` | TTL expiry, duplicate detection, capacity trim | Use small TTL and maxEntries to force cleanup |
| `TestPayloadTagger.cpp` | Idempotent tagging, non-JSON passthrough | Verify `_bridge` not duplicated |
| `TestThrottle.cpp` | Suppression window & final flush | Mock time with abstraction or coarse real sleeps with small windows |
| `TestQueue.cpp` | Heartbeat dedupe on/off; capacity drop | Configure small max size |
| `TestMetrics.cpp` | Counter increments & JSON serialization | Reset behavior |

### 6.2 Integration / Functional
- Simulated local publish sequence (mock or lightweight mosquitto) verifying AWS topic expansion & throttling.
- Simulated AWS inbound topics with multi-level wildcard verifying local template resolution.
- Disconnect / reconnect scenario ensuring queued messages drained.

### 6.3 Non-Functional
- Concurrency: Run high-frequency local publishes (thread) while AWS inbound also active; assert no deadlocks.
- Performance: Ensure throttling map & LoopGuard scale to configured `maxEntries` without O(n^2) behavior (cleanup is bounded). 

---
## 7. Metrics Specification
| Counter | Trigger | Reset Policy |
|---------|---------|--------------|
| `forwardedUp` | Successful publish local→AWS | Continuous (since start) |
| `forwardedDown` | Successful publish AWS→local | Continuous |
| `droppedLoop` | LoopGuard suppressed msg | Continuous |
| `droppedQos2` | QoS2 downgraded/dropped | Continuous |
| `queuedOffline` | Enqueued due to AWS offline | Continuous |
| `publishErrors` | Publish attempt failed | Continuous |
| `throttledMessages` | Suppressed by throttle | Continuous |

JSON: `{ "forwardedUp":N, ..., "uptimeSec":T }`.

---
## 8. Configuration Semantics Clarifications
| Field | Behavior |
|-------|----------|
| `routes[].throttleSeconds` | 0 = disabled; if >0 first message passes immediately; within window updates cached payload; at next allowed time send latest cached (if changed). |
| `queue.maxInMemory` | Ring-drop oldest when at capacity (instead of rejecting) – CHANGE PROPOSAL (safer) or keep reject & count. |
| `queue.dedupeHeartbeat` | If true, only last heartbeat per logical topic retained inside window (configurable window = `loopGuard.ttlSeconds` or dedicated field). |
| `metrics.publishIntervalSec` | If 0 and enabled → default to 60; if disabled ignore all metrics publishing but still count internally. |
| `local.useTLS` | If true and TLS not compiled/linked or CA data missing → log explicit warning & continue plaintext unless user overrides with fatal flag (future). |

---
## 9. Risk & Mitigations
| Risk | Mitigation |
|------|------------|
| Increased thread count (metrics + 2 forwarding) | Combine metrics into one of existing threads if resource constrained (optional). |
| Time-based throttling drift | Use steady_clock; store nextAllowed time. |
| Queue memory growth via large payloads | Enforce max payload size? (Out of scope now—log if >256KB). |
| Variable expansion errors (user mis-templates) | Validate placeholders: if `${matchN}` referenced without corresponding wildcard, log config error & disable route. |
| TLS partial implementation confusion | Clear log message summarizing state. |

---
## 10. Rollout / Migration Notes
1. Phase 1 patch merged quickly to restore build. 
2. Feature flag remains OFF by default; regression risk isolated. 
3. After full implementation, add section to release notes. 
4. Encourage end-users to re-validate config with new stricter validation (warn first release, enforce next). 

---
## 11. Acceptance Criteria (Final)
- All planned counters present & correctly incremented.
- Wildcard variable expansion works both directions.
- Throttling unit test validates suppression and deferred send.
- No literal `${matchN}` leaves code path to AWS.
- Offline messages delivered after reconnect in order preserved (modulo throttling). 
- Heartbeat dedupe obeys config flag; no hidden regex when disabled.
- QoS2 inbound/outbound never forwarded as QoS2; warning logged.
- Metrics published JSON valid and accepted by AWS (when topic configured and enabled).
- Clean build on Linux & Windows (with or without mosquitto installed). 
- All new code passes existing static analysis and style (headers with guards, no unused includes). 
- Documentation updated and accurate.

---
## 12. Work Breakdown (Task List)
1. Repair & refactor `LocalMqttBridgeFeature.cpp` (Phase 1). 
2. Implement up-route wildcard capture + expansion. 
3. Add throttling logic and tests. 
4. Implement metrics (counters + publisher). 
5. Rework Queue heartbeat dedupe honoring config. 
6. Add offline buffering & drain logic (AWS connectivity check). 
7. QoS2 detection + handling. 
8. Optional: symmetric tagging / down tagging. 
9. TLS usage hook (log or implement). 
10. Validation enhancements (placeholder mismatch). 
11. Tests for each module. 
12. Documentation + changelog entry. 
13. Windows CMake fallback. 
14. Final QA & polish.

---
## 13. Estimation (Relative)
| Task | Effort (Points) |
|------|-----------------|
| File repair & routing fix | 2 |
| Wildcard expansion up | 3 |
| Throttling + tests | 3 |
| Metrics infra | 4 |
| Queue heartbeat refactor | 2 |
| Offline queue semantics | 3 |
| QoS2 handling | 1 |
| Tagging symmetry | 1 |
| TLS hook | 2 |
| Validation upgrades | 2 |
| Test suite build-out | 4 |
| Docs & CMake portability | 2 |
| QA polish | 2 |
| Total | ~31 |

---
## 14. Open Questions / Future Enhancements
- Should we support route-level explicit `heartbeat: true`? (Would remove heuristic). 
- Add exponential backoff to local reconnect attempts? 
- Support persistent disk-backed queue for extended offline periods. 
- Expose metrics via local HTTP endpoint for observability. not requuired

---
## 15. Immediate Next Action
Implement Phase 1: repair corrupted file, fix AWS subscription handler, and add unit tests for RouteMatcher current behavior before layering new logic.

---
Prepared: 2025-09-29
