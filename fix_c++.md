# Tightrope C++ Fix Guide: Senior Engineer Audit

**Date:** 2026-04-01
**Verdict:** This codebase was initially audited at ~60% production-ready; after the fixes listed in the sync update, it is materially improved but still blocked on replication security, Raft membership semantics, and failure-mode coverage.

---

## Table of Contents

1. [Implementation Sync Update](#implementation-sync-update-2026-03-31-latest)
2. [SEVERITY P0 -- Data Loss / Crash Recovery Failures](#p0---data-loss--crash-recovery-failures)
3. [SEVERITY P1 -- Stub Files Shipping Zero Code](#p1---stub-files-shipping-zero-code)
4. [SEVERITY P2 -- Thread Safety Violations](#p2---thread-safety-violations)
5. [SEVERITY P3 -- Bridge / Frontend-Backend Gaps](#p3---bridge--frontend-backend-gaps)
6. [SEVERITY P4 -- Consensus Protocol Gaps](#p4---consensus-protocol-gaps)
7. [SEVERITY P5 -- Journaling / WAL Gaps](#p5---journaling--wal-gaps)
8. [SEVERITY P6 -- Replication / Networking Gaps](#p6---replication--networking-gaps)
9. [SEVERITY P7 -- Memory Management & Resource Leaks](#p7---memory-management--resource-leaks)
10. [SEVERITY P8 -- Error Handling & Validation](#p8---error-handling--validation)
11. [SEVERITY P9 -- Hardcoded Config & Missing Tunables](#p9---hardcoded-config--missing-tunables)
12. [SEVERITY P10 -- Test Coverage Gaps](#p10---test-coverage-gaps)
13. [Summary Scorecard](#summary-scorecard)

---

## Implementation Sync Update (2026-04-01, latest)

This document started as an audit snapshot. The codebase has moved since then. Current implementation status:

| Item | Status | Implementation | Verification |
|---|---|---|---|
| **P0-1** deterministic raft storage path | Done | `native/sync/src/consensus/nuraft_backend_common.cpp` | `[sync][raft][node][p0]` |
| **P0-2** state machine durability plumbing | Done | `native/sync/src/consensus/nuraft_backend_state.cpp`, `native/sync/src/consensus/nuraft_backend_storage.cpp` | `[sync][raft][node][p0]` |
| **P0-3** journal durability PRAGMAs | Done | `native/sync/src/sync_schema.cpp` | `[sync][schema][integration][p0]` |
| **P0-4** durable checkpoint mode | Done | `native/sync/src/consensus/nuraft_backend_storage.cpp` (`TRUNCATE`) | `[sync][raft][storage][p0]` |
| **P2-1** dashboard session manager thread safety | Done | `native/auth/dashboard/include/dashboard/session_manager.h`, `native/auth/dashboard/src/session_manager.cpp` | `[auth]` |
| **P2-2/P2-3** balancer picker thread safety | Done | `native/balancer/strategies/src/{weighted_round_robin.cpp,success_rate.cpp,power_of_two.cpp}` | `[balancer]` |
| **P3-1** bridge error propagation | Done | `native/bridge/src/{bridge.cpp,addon.cpp}` (`last_error` surfaced to JS) | `[bridge]` |
| **P3-2** degraded health status | Done | `native/bridge/src/addon.cpp` | `[bridge]` |
| **P3-3** async bridge timeout | Done | `native/bridge/src/addon.cpp` (30s timeout) | `[bridge]` |
| **P3-4** preload input validation | Done | `app/src/preload/index.ts` | `npm --prefix app run typecheck`, `npm --prefix app run test:unit` |
| **P4-1** NuRaft test mode in prod | Done | `native/sync/src/consensus/nuraft_backend.cpp` (`test_mode=false` default, configurable) | `[sync][raft][node]`, `[p0]` |
| **P4-2** membership changes through NuRaft reconfiguration APIs | Done (with deferred retry semantics for in-flight config changes) | `native/sync/src/consensus/{nuraft_backend.cpp,raft_node.cpp,membership.cpp}`, `native/bridge/src/bridge.cpp` | `[bridge][lifecycle]`, `[sync][raft][node]`, `[sync][raft][membership]` |
| **P4-3** leader proxy no longer advertises unimplemented forwarding path | Done (safety mitigation) | `native/sync/src/consensus/leader_proxy.cpp`, `native/sync/include/consensus/leader_proxy.h` | `[sync][raft][proxy]` |
| **P4-4** read linearizability guardrails | Partial (baseline) | `native/server/controllers/{include/src}/linearizable_read_guard.*`, `native/server/controllers/src/{settings_controller.cpp,auth_controller.cpp,accounts_controller.cpp,keys_controller.cpp}`, `native/server/middleware/src/api_key_filter.cpp`, `native/bridge/src/bridge.cpp` | `[server][linearizable]`, `[server][settings]`, `[server][middleware][api-key][models]`, `[bridge][lifecycle]` |
| **P4-5** dead consensus implementations excluded from production binary | Done | `CMakeLists.txt` (`raft_log.cpp`, `raft_rpc.cpp`, `raft_scheduler.cpp` test-only linkage) | `tightrope-core` build + `[sync][raft][log]`, `[sync][raft][timer]` |
| **P5-1** automatic journal compaction trigger | Done | `native/sync/src/persistent_journal.cpp` (append-time threshold/interval/retention auto-compact with ack gate; env tunables) | `[sync][pjournal][integration]` |
| **P5-2** post-compaction reclaim (`VACUUM`) | Done | `native/sync/src/persistent_journal.cpp` | `[sync][pjournal][integration]` |
| **P5-3** local-read checksum verification | Done | `native/sync/src/persistent_journal.cpp` (`entries_after` checksum validation; `TIGHTROPE_SYNC_JOURNAL_VERIFY_CHECKSUM_ON_READ`) | `[sync][pjournal][integration]` |
| **P5-4** crash-corruption sync metadata recovery | Done (startup guardrails baseline) | `native/sync/src/sync_schema.cpp` (`PRAGMA integrity_check` on startup + malformed sync-metadata detection with guarded table rebuild via `TIGHTROPE_SYNC_SCHEMA_AUTO_REBUILD_ON_CORRUPTION`) | `[sync][schema][integration][p5]` |
| **P6-1/P6-2** handshake auth + TLS peer verification primitives | Partial (live ingress + lifecycle probes + strict fail-closed enforcement + telemetry surfaced) | `native/sync/src/{sync_protocol.cpp,sync_engine.cpp}`, `native/sync/src/transport/{tls_stream.cpp,replication_ingress.cpp,replication_socket_server.cpp,peer_probe.cpp}`, `native/bridge/src/{bridge.cpp,addon_bindings_support.cpp}` + settings plumbing (`native/{db/repositories,server/controllers,server/src}/...`, `app/src/renderer/components/settings/sections/DatabaseSyncSection.tsx`) for runtime peer-security gating + config surfacing; probe telemetry includes per-attempt duration (`last_probe_duration_ms`), ingress rejection reason/counters, and richer socket/listener telemetry (accept/read/apply/TLS/ack failures, bytes, active/peak connections, connection-duration totals/max/last/ewma + histogram buckets, queue saturation highs, pause cycles, last failure) | `[sync][protocol]`, `[sync][engine][integration]`, `[sync][transport][tls]`, `[sync][transport][replication_ingress]`, `[sync][transport][replication_socket]`, `[sync][transport][peer_probe]`, `[bridge][lifecycle]`, `[server][settings]`, `[server][runtime][admin][settings]`, `npm --prefix app run {typecheck,test:unit}` |
| **P6-3** inbound replication backpressure/rate limiting | Done (baseline live ingress path) | `native/sync/src/sync_engine.cpp`, `native/sync/src/transport/{rpc_channel.cpp,replication_ingress.cpp,replication_socket_server.cpp}`, `native/bridge/src/bridge.cpp` (max wire bytes/entries, in-flight budgets, per-peer token bucket, bounded RPC ingress queue, handshake-gated replication ingress session, live socket read/drain backpressure loop) | `[sync][engine][integration]`, `[sync][transport][rpc]`, `[sync][transport][replication_ingress]`, `[sync][transport][replication_socket]`, `[bridge][lifecycle]` |
| **P6-4/P6-5** dead-peer eviction + lag observability | Partial (baseline++) | `native/bridge/src/bridge.cpp`, `native/sync/src/sync_engine.cpp`, `native/sync/src/discovery/peer_manager.cpp`, `native/sync/src/transport/replication_socket_server.cpp`, `native/bridge/src/addon_bindings_support.cpp`, `app/src/renderer/components/settings/sections/DatabaseSyncSection.tsx` (includes per-peer probe duration telemetry + ingress accepted/rejected batch/byte counters + categorized ingress rejection counters/reason + per-peer apply-duration trend telemetry (`total/last/max/ewma/samples`) + per-peer end-to-end replication-latency trend telemetry (`total/last/max/ewma/samples`, derived from wire-entry `hlc_wall` to local apply time) + per-peer in-flight fairness/concurrency telemetry (`current/peak` wire batches/bytes) + cluster-level lag summary + sustained-alert telemetry (`lagging/total/max/avg/ewma`, threshold/sustained refreshes, streak, last alert)) | `[bridge][lifecycle]`, `[sync][engine][integration]`, `[sync][transport][replication_socket]`, `npm --prefix app run {typecheck,test:unit}` |
| **P1** crypto/key stubs (`fernet`, `key_file`) | Done (module-level) | `native/auth/crypto/{include,src}/{fernet,key_file}.*` | `[auth][crypto]` |
| **P1** runtime token-at-rest encryption integration | Partial (auto-strict default + migration + inventory dry-run + guarded admin bulk migration baseline) | `native/auth/crypto/{include,src}/token_store.*`, `native/db/repositories/src/account_repo.cpp`, `native/auth/oauth/src/token_refresh.cpp`, `native/proxy/session/src/sticky_affinity.cpp`, `native/server/controllers/src/accounts_controller.cpp`, `native/server/src/admin_accounts_runtime.cpp` | `[auth][crypto][token-store]`, `[auth][oauth][refresh][crypto]`, `[proxy][auth][credentials][crypto]`, `[server][accounts][crypto]`, `[server][runtime][admin][accounts]` |
| **P1** core json/time stubs (`serialize`, `clock`, `ewma`) | Done (module-level) | `native/core/json/{include,src}/serialize.*`, `native/core/time/include/time/{clock,ewma}.h` | `[core][json][serialize]`, `[core][time][clock]`, `[core][time][ewma]` |
| **P1** cost calculator integration | Done (runtime integrated + authoritative settings/env pricing feed + account-level 24h request/cost aggregation) | `native/usage/include/cost_calculator.h`, `native/usage/src/cost_calculator.cpp`, `native/proxy/session/src/sticky_affinity.cpp`, `native/proxy/src/{proxy_service.cpp,ws_proxy.cpp}`, `native/{db/repositories,server/controllers,server/src}/...` (`request_logs` aggregation surfaced on `/api/accounts` as `requests24h`, `totalCost24hUsd`, `costNorm`; runtime pricing overrides now persisted via dashboard settings `routing_plan_model_pricing_usd_per_million` and merged with env override precedence) + settings UI/API wiring (`app/src/renderer/components/settings/sections/RoutingOptionsSection.tsx`, `app/src/renderer/state/useTightropeState.ts`, `native/server/src/admin_settings_runtime.cpp`) | `[usage][cost]`, `[proxy][auth][credentials][routing][cost]`, `[server][settings]`, `[server][runtime][admin][settings]`, `[db][request-log]`, `[server][accounts][usage]` |
| **P6-6** protocol version mismatch handling | Partial (live ingress + lifecycle probes + fail-closed controls + telemetry surfaced) | `native/sync/src/sync_protocol.cpp`, `native/sync/include/sync_protocol.h`, `native/sync/src/sync_engine.cpp`, `native/sync/src/transport/{replication_ingress.cpp,replication_socket_server.cpp,peer_probe.cpp}` (validation + downgrade policy + wire-apply + handshake-first socket ingress enforcement path + outbound handshake ack verification) | `[sync][protocol]`, `[sync][engine][integration]`, `[sync][transport][replication_socket]`, `[sync][transport][peer_probe]` |
| **P7-2** raw `new/delete` backend impl ptr | Done | `native/sync/include/consensus/nuraft_backend.h`, `native/sync/src/consensus/nuraft_backend.cpp` (`std::unique_ptr`) | `tightrope-tests build` |
| **P8-1** ignored DB return values | Done | `native/db/repositories/src/api_key_repo_limits.cpp` | `[db][api-key]` |
| **P8-2** unsafe SQLite text conversion sites | Done | `native/auth/oauth/src/token_refresh.cpp` | `[oauth]` |
| **P8-3** rollback failure silent ignore | Done | `native/sync/src/consensus/nuraft_backend_storage.cpp` (rollback failure context in `last_error_`) | `[sync][raft][storage]` |
| **P8-4** password hash validation | Done | `native/auth/dashboard/src/password_auth.cpp` (argon prefix + NUL guard) | `[auth][dashboard][password]`, `[auth]` |
| **P9-1/P9-3** Raft tunables + thread pool tunable | Done | `native/sync/{include/src}/consensus/*`, `native/bridge/src/{bridge.cpp,addon_bindings_support.cpp}`, `app/src/main/native.ts` | `[sync][raft][node]`, `[bridge]` |
| **P9-2** sticky TTL/cleanup tunables | Done | `native/config/{include/src}/*`, `native/proxy/session/src/sticky_affinity.cpp` | `[config]`, `[proxy][sticky]` |
| **P9-4** discovery host routability validation | Done | `native/bridge/src/bridge.cpp`, `native/tests/bridge/bridge_lifecycle_test.cpp` | `[bridge]` |
| **Proxy chat tool-arg replay dedupe** (`codex-lb` #284 parity) | Done | `native/proxy/src/chat_completions_service.cpp` (snapshot-aware tool-call argument normalization) | `[server][runtime][proxy][chat]` |
| **Auth flake (post-audit)** runtime port collisions | Done | `native/tests/server/src/runtime_test_utils.cpp` (dedicated bindable-range allocator) | elevated `[auth]` loops: 20/20 repeatedly |

Remaining highest-risk open areas: `P1` rollout/cleanup polish (clock/EWMA adoption breadth + pricing freshness operations), deeper wire/socket telemetry for `P6-1/P6-2/P6-6` (true multi-connection fairness policy/attribution beyond current per-peer in-flight current/peak counters), `P6-4/P6-5` alert externalization (export hooks/runbook integration beyond current in-cluster sustained lag summary), and most of `P10`.

---

## P0 - Data Loss / Crash Recovery Failures

### P0 Implementation Status (2026-03-31)

| Item | Status | Implementation | Test Coverage |
|---|---|---|---|
| **P0-1** deterministic storage path | Done | `native/sync/src/consensus/nuraft_backend_common.cpp` | `native/tests/unit/sync/raft_node_test.cpp` (`[sync][raft][node][p0]`) |
| **P0-2** state machine persistence | Done | `native/sync/src/consensus/nuraft_backend_state.cpp`, `native/sync/src/consensus/nuraft_backend_storage.cpp` | `native/tests/unit/sync/raft_node_test.cpp` (`[sync][raft][node][p0]`) |
| **P0-3** journal durability PRAGMAs | Done | `native/sync/src/sync_schema.cpp` | `native/tests/integration/sync/sync_schema_test.cpp` (`[sync][schema][integration][p0]`) |
| **P0-4** durable checkpoint mode (`TRUNCATE`) | Done | `native/sync/src/consensus/nuraft_backend_storage.cpp` | `native/tests/unit/sync/nuraft_backend_storage_test.cpp` (`[sync][raft][storage][p0]`) |

**Build verification note (Windows):** `tightrope-core` builds successfully after restoring the missing `SqliteRaftStorage` forward declaration in `native/sync/include/consensus/internal/nuraft_backend_components.h`. The aggregate `tightrope-tests` target still fails on existing POSIX-only tests (`setenv`, `unsetenv`, `unistd.h`, `arpa/inet.h`) that are unrelated to this P0 work.

### P0-1: Raft Storage Path is Non-Deterministic -- DATA LOSS ON RESTART

**File:** `native/sync/src/consensus/nuraft_backend_common.cpp:56-71`

```cpp
std::string make_storage_path(const std::uint32_t node_id, const std::uint16_t port_base) {
    static std::atomic<std::uint64_t> sequence{1};  // <-- INCREMENTS EVERY CALL
    // ...
    const auto run_id = sequence.fetch_add(1);
    const auto filename = "nuraft-" + std::to_string(port_base) + "-" +
                          std::to_string(node_id) + "-" + std::to_string(run_id) + ".db";
    return (root / filename).string();
}
```

**Impact:** Every time a `Backend` is constructed, it gets a NEW database file. All previous Raft log entries, committed state, term, and vote records are **permanently orphaned**. The node restarts as if it never existed. This is a **total data loss bug** masked by short-lived tests.

**Fix:** Storage path must be deterministic based on `(cluster_name, node_id)`. Remove the atomic sequence counter entirely. Pass the path from the `BridgeConfig` or derive it from `db_path`:

```cpp
std::string make_storage_path(const std::string& base_dir, std::uint32_t node_id) {
    auto root = std::filesystem::path(base_dir) / "raft";
    std::filesystem::create_directories(root);
    return (root / ("raft-node-" + std::to_string(node_id) + ".db")).string();
}
```

---

### P0-2: State Machine is In-Memory Only -- COMMITTED STATE LOST ON CRASH

**File:** `native/sync/src/consensus/nuraft_backend_state.cpp:19-26`

```cpp
nuraft::ptr<nuraft::buffer> InMemoryStateMachine::commit(const nuraft::ulong log_idx, nuraft::buffer& data) {
    // ...
    committed_payloads_.emplace_back(reinterpret_cast<const char*>(bytes), size);
    commit_index_.store(log_idx);
    return nullptr;
}
```

`committed_payloads_` is `std::vector<std::string>` -- pure heap memory. When the process exits, all applied entries evaporate. After restart + log replay, the state machine will re-apply entries, but only if the log still exists (see P0-1 above -- it won't).

**Fix:** The state machine must either:
1. Apply entries directly to SQLite (the application database), or
2. Persist its state to a durable snapshot file that survives restart.

Option 1 is correct for this architecture -- committed Raft payloads should be applied to the main `store.db` as SQL mutations.

---

### P0-3: Main Journal Database Lacks Durability PRAGMAs

**Files:** `native/sync/src/persistent_journal.cpp`, `native/sync/src/sync_schema.cpp`

The Raft storage correctly sets `PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;` (in `nuraft_backend_storage.cpp:39`). But the main `_sync_journal` table lives on a **different** `sqlite3*` connection (`db_` in PersistentJournal) that **never** sets these PRAGMAs.

**Impact:** SQLite defaults to DELETE journal mode with `synchronous=FULL`, which is safe but slow. If the connection is opened with `synchronous=NORMAL` (a common optimization), journal entries can be lost on power failure. This is an implicit dependency on SQLite defaults that must be made explicit.

**Fix:** In `sync_schema.cpp::ensure_sync_schema()` or wherever the main DB connection is opened, add:

```cpp
exec_sql(db, "PRAGMA journal_mode=WAL;");
exec_sql(db, "PRAGMA synchronous=FULL;");
```

---

### P0-4: `flush()` Uses PASSIVE Checkpoint -- Not Durable

**File:** `native/sync/src/consensus/nuraft_backend_storage.cpp:237-240`

```cpp
bool SqliteRaftStorage::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    return exec_locked("PRAGMA wal_checkpoint(PASSIVE);");
}
```

`PASSIVE` is non-blocking and may not checkpoint all frames. A crash between commit and checkpoint loses data in WAL frames.

**Fix:** Use `TRUNCATE` or `RESTART` for explicit flush calls:

```cpp
return exec_locked("PRAGMA wal_checkpoint(TRUNCATE);");
```

---

## P1 - Stub Files Shipping Zero Code

This section started with all-placeholder modules. It is now mixed: some modules are implemented, while others remain stubs or are not wired into runtime paths.

| Header File | Matching .cpp | Status | What It Claims To Be |
|---|---|---|---|
| `native/auth/crypto/include/crypto/fernet.h` | `native/auth/crypto/src/fernet.cpp` | Implemented (module-level) | Fernet-compatible encrypt/decrypt |
| `native/auth/crypto/include/crypto/key_file.h` | `native/auth/crypto/src/key_file.cpp` | Implemented (module-level) | Encryption key file I/O |
| `native/core/json/include/json/serialize.h` | `native/core/json/src/serialize.cpp` | Implemented (module-level) | JSON serialization templates |
| `native/core/time/include/time/clock.h` | (header-only) | Implemented (module-level) | Mockable clock abstraction |
| `native/core/time/include/time/ewma.h` | (header-only) | Implemented (module-level) | EWMA template |
| `native/usage/include/cost_calculator.h` | `native/usage/src/cost_calculator.cpp` | Implemented + baseline integrated | Per-request cost estimation |
| `native/bridge/include/bridge_helpers.h` | (removed) | Done (cleanup) | Deprecated N-API <-> C++ type conversion placeholder (deleted) |

**Implementation update (2026-04-01):**
- `cost_calculator` implemented with unit coverage (`native/tests/unit/usage/cost_calculator_test.cpp`) for input/output/total USD estimation and negative-price clamping.
- `fernet` and `key_file` modules implemented with libsodium secretbox + argon2id-derived key wrapping and unit coverage (`native/tests/auth/crypto_key_file_test.cpp`).
- Runtime token-at-rest integration now includes strict-mode + migration baseline via `token_store` and call-site wiring in OAuth/account/proxy paths. Strict mode (`TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST`) rejects plaintext at rest when enabled, and now defaults to strict when token-encryption key material is configured unless explicitly overridden; migration-on-read (`TIGHTROPE_TOKEN_ENCRYPTION_MIGRATE_PLAINTEXT_ON_READ`, default on) opportunistically re-encrypts legacy plaintext rows when keys are available. A bulk admin migration path is available through `/api/accounts/migrate-token-storage` with dry-run inventory support (`?dryRun=true`) and plaintext-account/token counters; non-dry-run migration is now guarded on encryption-key readiness (`native/tests/auth/token_store_test.cpp`, `native/tests/auth/oauth_token_refresh_test.cpp`, `native/tests/integration/proxy/proxy_account_credentials_test.cpp`, `native/tests/server/accounts_controller_test.cpp`, `native/tests/server/server_admin_runtime_test.cpp`).
- `serialize`, `clock`, and `ewma` are now implemented with baseline coverage (`native/tests/core/time_json_modules_test.cpp`).
- `clock` abstraction adoption expanded beyond module baseline into additional time-sensitive runtime paths (`native/server/controllers/src/{auth_controller.cpp,accounts_controller.cpp}`, `native/server/src/admin_accounts_runtime.cpp`, `native/proxy/session/src/{sticky_affinity.cpp,http_bridge.cpp}`, `native/proxy/src/account_traffic.cpp`, `native/{bridge/src/bridge.cpp,sync/src/sync_engine.cpp}`), replacing direct wall-clock calls with `core::time::Clock` (`[server][settings]`, `[server][runtime][admin][accounts]`, `[proxy][auth][credentials][routing][cost]`, `[sync][engine][integration]`, `[bridge][lifecycle]`).
- `ewma` helper adoption now includes live ingress socket connection duration smoothing and per-peer ingress apply-duration + replication-latency smoothing (`native/sync/src/transport/replication_socket_server.cpp`, `native/sync/src/sync_engine.cpp`, `native/bridge/src/bridge.cpp`, `native/bridge/src/addon_bindings_support.cpp`, `app/src/renderer/components/settings/sections/DatabaseSyncSection.tsx`, `native/tests/integration/sync/{replication_socket_server_test.cpp,sync_engine_test.cpp}`).
- `cost_calculator` now participates in runtime account routing (`cost_aware`) through plan-pricing normalization in sticky affinity, with request-model-aware scoring and authoritative runtime plan/model pricing overrides persisted via dashboard settings (`routing_plan_model_pricing_usd_per_million`) plus env precedence (`TIGHTROPE_ROUTING_PLAN_MODEL_PRICING_USD_PER_MILLION`) (`native/proxy/session/src/sticky_affinity.cpp`, `native/db/repositories/src/settings_repo.cpp`, `native/server/controllers/src/settings_controller.cpp`, `native/server/src/admin_settings_runtime.cpp`, `app/src/renderer/components/settings/sections/RoutingOptionsSection.tsx`, `native/tests/integration/proxy/proxy_account_credentials_test.cpp`, `native/tests/server/{settings_controller_test.cpp,server_admin_runtime_test.cpp}`).
- Account-level request/cost aggregation is now surfaced on `/api/accounts` from `request_logs` (`requests24h`, `totalCost24hUsd`, normalized `costNorm`) (`native/db/repositories/src/request_log_repo.cpp`, `native/server/controllers/src/accounts_controller.cpp`, `native/server/src/admin_accounts_runtime.cpp`, `native/tests/db/request_log_repo_test.cpp`, `native/tests/server/accounts_controller_test.cpp`).
- Remaining P1 risk is now broader adoption polish for newly added utility baselines plus pricing freshness operational cadence.

**Impact by remaining gap:**

- **fernet.h / key_file.h rollout gap**: strict-mode, on-read migration, and admin bulk migration endpoint now include auto-strict defaults (when key material is configured), dry-run inventory output, and guarded live migration; remaining work is operational rollout cadence/monitoring discipline.
- **serialize.h**: Baseline JSON parse/write + typed serialize/deserialize helpers are now present over `glaze`; remaining risk is adoption consistency.
- **clock.h**: Baseline `SystemClock` + `ManualClock` abstraction now exists and is adopted in key runtime controller/proxy + bridge/sync telemetry paths; remaining risk is deeper adoption across remaining sync/network internals.
- **ewma.h**: EWMA utility template now exists with clamp behavior tests and is now wired into ingress connection/apply/replication-latency trends; remaining risk is adoption where smoothing remains duplicated or implicit.
- **cost_calculator.h**: Runtime wiring now supports model-aware plan-cost normalization, dashboard/API/UI-managed plan/model pricing overrides (with env precedence), and account-level 24h request/cost aggregation; remaining risk is pricing-data freshness governance (runbook/monitoring cadence).

**Fix (remaining):**
1. **Operationalize pricing freshness**: keep current runtime override inputs and add runbook/monitoring discipline for pricing refresh cadence.
2. **Continue clock abstraction adoption** in remaining sync/networking time-sensitive paths.
3. **Adopt EWMA helper** where latency smoothing logic is duplicated or implicit.

---

## P2 - Thread Safety Violations

### P2-1: `DashboardSessionManager` -- Unprotected Concurrent Map Access

**File:** `native/auth/dashboard/include/dashboard/session_manager.h:28-29`

```cpp
private:
    std::int64_t ttl_ms_ = 12 * 60 * 60 * 1000;
    std::unordered_map<std::string, DashboardSessionState> sessions_;
```

`create()`, `get()`, `erase()`, and `purge_expired()` all touch `sessions_` with **zero synchronization**. Multiple HTTP request threads calling these concurrently will corrupt the map, crash with iterator invalidation, or produce undefined behavior.

**Fix:** Add `mutable std::mutex mutex_;` and lock in every public method, or use a `std::shared_mutex` for read-heavy workloads (get vs create/erase).

---

### P2-2: `WeightedRoundRobinPicker` -- Unprotected Mutable State

**File:** `native/balancer/strategies/include/strategies/weighted_round_robin.h:20`

```cpp
private:
    std::unordered_map<std::string, double> current_weights_;
```

`pick()` modifies `current_weights_` on every call. If the load balancer is called from multiple request-handling threads (which it will be), this is a data race.

**Fix:** Either make the picker per-thread, or add a mutex.

---

### P2-3: RNG in Balancer Strategies -- Not Thread-Safe

**Files:**
- `native/balancer/strategies/src/success_rate.cpp` -- `std::mt19937 rng_;`
- `native/balancer/strategies/src/power_of_two.cpp` -- `std::mt19937 rng_;`

`std::mt19937` is not thread-safe. Concurrent calls to `pick()` from different request threads will corrupt the RNG state.

**Fix:** Use `thread_local std::mt19937` or protect with a mutex. Thread-local is preferred for performance:

```cpp
thread_local std::mt19937 rng_{std::random_device{}()};
```

---

## P3 - Bridge / Frontend-Backend Gaps

### P3-1: Generic Error Messages Lose All Context

**File:** `native/bridge/src/addon.cpp:92-113`

Every `queue_async_void` call uses a generic error string like `"clusterEnable failed"`, `"bridge init failed"`, etc. When the C++ `Bridge` method returns `false`, the JS side gets zero detail about **why**.

```cpp
// Current: user sees "clusterEnable failed"
// Should see: "clusterEnable failed: port 26001 already in use (EADDRINUSE)"
```

**Fix:** Change `Bridge` methods from `bool` returns to `std::expected<void, std::string>` or at minimum provide a `last_error()` method. Propagate the actual error message through the async worker.

---

### P3-2: Health Status Never Reports "degraded"

**File:** `native/bridge/src/addon.cpp:180`

```cpp
object.Set("status", snapshot.running ? "ok" : "error");
```

The preload TypeScript declares `'ok' | 'degraded' | 'error'` but C++ only emits `"ok"` or `"error"`. There is no degraded state (e.g., cluster enabled but no quorum, high error rate, approaching usage limits).

**Fix:** Add degraded detection:

```cpp
if (!snapshot.running) return "error";
if (cluster_enabled && !has_quorum) return "degraded";
return "ok";
```

---

### P3-3: No Async Operation Timeouts

**File:** `native/bridge/src/addon.cpp:19-63`

`AsyncBridgeWorker` has no timeout mechanism. If a native operation hangs (e.g., Raft election that never completes, network call that blocks indefinitely), the JS Promise never resolves. The UI freezes or the operation silently disappears.

**Fix:** Add a deadline to async workers. Use `std::condition_variable::wait_for` or a separate watchdog timer. Cancel the operation and reject the Promise after timeout.

---

### P3-4: No Input Validation in Preload Layer

**File:** `app/src/preload/index.ts`

- `addFirewallIp(ipAddress: string)` -- no IP format validation
- `addPeer(address: string)` -- no `host:port` format validation
- `removePeer(siteId: string)` -- no format validation

Invalid input passes through IPC, through the bridge, and into C++ before being caught (if at all).

**Fix:** Validate at the boundary:

```typescript
addFirewallIp: (ip: string) => {
    if (!/^\d{1,3}(\.\d{1,3}){3}(\/\d{1,2})?$/.test(ip)) {
        return Promise.reject(new Error('Invalid IP format'));
    }
    return ipcRenderer.invoke('firewall:add', ip);
}
```

---

### P3-5: Missing Progress Reporting / Cancellation for Long Operations

The bridge has no mechanism for:
- Reporting sync progress (% of journal entries replicated)
- Cancelling a long-running sync or cluster join
- Streaming cluster state changes to the UI

**Fix:** Add an IPC event channel (`ipcMain.emit` / `ipcRenderer.on`) for push-based status updates from native code.

---

## P4 - Consensus Protocol Gaps

### P4-1: `test_mode_flag_` is TRUE in Production

**File:** `native/sync/src/consensus/nuraft_backend.cpp:105`

```cpp
init_options.test_mode_flag_ = true;
```

NuRaft's test mode relaxes safety checks. This must be `false` in production.

**Fix:** Make this configurable, default to `false`.

---

### P4-2: Membership Changes Not Replicated Through Raft

**File:** `native/sync/src/consensus/membership.cpp`

`begin_joint_consensus()` and `commit_joint_consensus()` modify local state only. They do **not** submit membership changes through NuRaft's configuration change API. A follower could locally commit a config change that the leader has not agreed to.

This violates Raft safety: membership changes must go through the log.

**Fix:** Use NuRaft's `raft_server::add_srv()` and `raft_server::remove_srv()` APIs for membership changes. The local `Membership` class should be a read-only view of the NuRaft cluster config.

**Implementation update (2026-03-31):** `RaftNode::add_member/remove_member` now call NuRaft-backed `Backend::add_server/remove_server`, and `Membership` is refreshed from `get_srv_config_all()` via `reset_members(...)`. Bridge peer add/remove now routes through this path with leader-retry and bounded in-flight config-change retries. `add/remove` idempotency handling was added for NuRaft `already exists` / `cannot find server` responses to avoid spurious failures during asynchronous reconfiguration windows.

---

### P4-3: Leader Proxy Has No Forwarding Implementation

**File:** `native/sync/src/consensus/leader_proxy.cpp`

`decide()` returns `LeaderProxyAction::ForwardToLeader` but there is **no RPC client code** to actually forward the request. The caller must implement forwarding, but no evidence of this exists.

**Fix:** Implement an RPC client that forwards proposals to the current leader, with timeout and retry logic.

**Implementation update (2026-03-31):** Until a concrete RPC transport path is introduced, `LeaderProxy` now explicitly returns `RejectForwardingUnavailable` (with `target_leader_id`) instead of returning `ForwardToLeader` for an unimplemented path. This prevents false-positive "forwarded" decisions and makes caller behavior explicit.

---

### P4-4: No Read Linearizability

Reads go directly to the application database without going through the leader. A follower can return stale data.

**Fix:** For strongly-consistent tables (`RaftLinearizable` strategy), implement ReadIndex or lease-based reads through the Raft leader.

**Implementation update (2026-03-31):** A baseline leader-only linearizable-read gate is now enforced for Raft-linearizable tables:
- new `check_linearizable_read_access(...)` guard in `native/server/controllers/src/linearizable_read_guard.cpp`
- guard wired into settings/auth/accounts/keys controllers and API-key middleware before raft-linearizable table reads
- rejection uses `503` + `linearizable_read_requires_leader` (with leader id when known)

This closes the unsafe follower-read default. Remaining `P4-4` work is full ReadIndex/lease semantics or explicit follower-forwarding when read availability under leader failover is required.

---

### P4-5: Dead Code in Consensus Module

These files are **never called in production** because NuRaft handles the logic internally:

| File | What It Is | Status |
|---|---|---|
| `native/sync/src/consensus/raft_log.cpp` | In-memory Raft log | Test-only, dead in production |
| `native/sync/src/consensus/raft_rpc.cpp` | Vote/append RPC logic | Duplicate of NuRaft internals |
| `native/sync/src/consensus/raft_scheduler.cpp` | Election/heartbeat timers | Not used by `RaftNode` |

**Fix:** Move to a `test/` directory or delete. Dead code in `src/` is a maintenance trap.

**Implementation update (2026-03-31):** These legacy consensus units are now test-only (excluded from `tightrope-core` and linked only into `tightrope-core-testlib`): `native/sync/src/consensus/{raft_log.cpp,raft_rpc.cpp,raft_scheduler.cpp}`. This removes unused production surface while preserving unit coverage.

---

## P5 - Journaling / WAL Gaps

### P5-1: No Automatic Journal Compaction

**File:** `native/sync/src/persistent_journal.cpp:334-378`

`compact()` exists but is never called automatically. The `_sync_journal` table grows unbounded. On a high-throughput node, this will consume all disk space.

**Fix:** Add a background compaction timer:
- Trigger when journal exceeds N entries or M megabytes
- Compact entries older than T seconds AND acknowledged by all peers
- Run `PRAGMA incremental_vacuum` or periodic `VACUUM` to reclaim space

**Implementation update (2026-03-31):** `PersistentJournal::append` now runs an automatic compaction check on a bounded interval. When configured thresholds are met, it compacts entries older than retention and at/below the max acknowledged sequence (`applied >= min_applied_value`) and then uses the existing `VACUUM` path. Tunables are exposed via env:
- `TIGHTROPE_SYNC_JOURNAL_AUTO_COMPACT_ENABLED` (default `1`)
- `TIGHTROPE_SYNC_JOURNAL_AUTO_COMPACT_MIN_ENTRIES` (default `10000`)
- `TIGHTROPE_SYNC_JOURNAL_AUTO_COMPACT_INTERVAL` (default `256`)
- `TIGHTROPE_SYNC_JOURNAL_AUTO_COMPACT_RETENTION_MS` (default `300000`)
- `TIGHTROPE_SYNC_JOURNAL_AUTO_COMPACT_MIN_APPLIED` (default `2`)

---

### P5-2: No VACUUM After Compaction

SQLite `DELETE` does not reclaim disk space. After `compact()` deletes thousands of rows, the database file stays the same size.

**Fix:** After compaction, run:

```cpp
exec_sql(db, "PRAGMA incremental_vacuum;"); // if auto_vacuum=INCREMENTAL is set
// OR periodically:
exec_sql(db, "VACUUM;"); // expensive, do off-peak
```

---

### P5-3: Checksums Not Verified on Local Read

`journal_checksum()` is computed on append and verified on remote apply (in `sync_engine.cpp:268`), but **never verified when reading local entries**. Silent corruption in the local database goes undetected until a peer tries to apply it and rejects it.

**Fix:** Add optional checksum verification in `PersistentJournal::entries_after()`.

**Implementation update (2026-03-31):** `PersistentJournal::entries_after()` now verifies each row checksum against recomputed values before returning entries. On mismatch, it fails closed (returns no entries) and logs `entries_after_checksum_mismatch` with sequence + expected/actual checksum. Verification is enabled by default and can be toggled via `TIGHTROPE_SYNC_JOURNAL_VERIFY_CHECKSUM_ON_READ` (set `0` to disable for diagnostics/perf testing). Coverage added in `native/tests/integration/sync/persistent_journal_test.cpp`.

---

### P5-4: No Crash Recovery for Main Journal

There is no replay mechanism for the `_sync_journal` table. If the database is corrupted (torn write in non-WAL mode, disk error), there is no way to recover.

**Fix:**
1. Ensure WAL mode is set (P0-3 above)
2. Run `PRAGMA integrity_check` on startup
3. If corruption detected, rebuild from peer snapshot

**Implementation update (2026-03-31):** `ensure_sync_schema()` now runs startup `PRAGMA integrity_check` by default (`TIGHTROPE_SYNC_SCHEMA_INTEGRITY_CHECK_ON_STARTUP`, default `1`), validates required `_sync_journal` / `_sync_tombstones` columns, and can auto-rebuild malformed sync metadata tables when corruption recovery is enabled (`TIGHTROPE_SYNC_SCHEMA_AUTO_REBUILD_ON_CORRUPTION`, default `1`). Recovery-disabled mode fails closed on malformed metadata so startup cannot proceed with an invalid sync journal layout.

---

## P6 - Replication / Networking Gaps

### P6 Implementation Status (2026-04-01)

| Item | Status | Implementation | Test Coverage |
|---|---|---|---|
| **P6-1** peer handshake authentication | Partial (live ingress + lifecycle probes + fail-closed controls + telemetry surfaced) | `HandshakeFrame` carries `auth_key_id` + `auth_hmac_hex`; `sign_handshake` + `validate_handshake_auth`; wire-apply gate in `SyncEngine`; `ReplicationIngressSession` enforces handshake-first ingress before replication frames; `ReplicationSocketServer` emits handshake ACK on acceptance; `peer_probe` validates outbound handshake acceptance with bounded timeout; `Bridge::refresh_cluster_peers` runs throttled lifecycle probes by default and can enforce fail-closed behavior (`TIGHTROPE_SYNC_PEER_PROBE_FAIL_CLOSED{,_FAILURES}`), folding probe failures into peer state and eviction decisions | `native/tests/unit/sync/sync_protocol_test.cpp`, `native/tests/integration/sync/sync_engine_test.cpp`, `native/tests/integration/sync/replication_ingress_test.cpp`, `native/tests/integration/sync/replication_socket_server_test.cpp`, `native/tests/integration/sync/peer_probe_test.cpp`, `native/tests/bridge/bridge_lifecycle_test.cpp`, `native/tests/server/settings_controller_test.cpp` |
| **P6-2** TLS context + peer verification | Partial (live ingress + lifecycle probes + fail-closed controls + telemetry surfaced) | `TlsStream` wraps `boost::asio::ssl::stream` with cert loading + verify + pinning; `ReplicationSocketServer` runs live TCP/TLS ingress loop and `Bridge::cluster_enable` binds listener TLS mode directly to cluster TLS config (not peer-list presence); `peer_probe` supports timed TLS handshake probing and `Bridge::cluster_add_peer` can still enforce add-time preflight via `TIGHTROPE_SYNC_PROBE_PEER_ON_ADD=1`; runtime cluster rejects peer networking when TLS/verify constraints are disabled and exposes TLS config in admin/renderer settings | `native/tests/unit/sync/tls_stream_test.cpp`, `native/tests/integration/sync/replication_socket_server_test.cpp`, `native/tests/integration/sync/peer_probe_test.cpp`, `native/tests/bridge/bridge_lifecycle_test.cpp`, `native/tests/server/server_admin_runtime_test.cpp`, `app/src/renderer/App.test.tsx` |
| **P6-3** backpressure/rate-limit guardrails | Done (baseline live ingress path) | `SyncEngine::apply_wire_batch` enforces max wire bytes, max entry count, max in-flight batches, global/per-peer in-flight wire-byte budgets, per-peer in-flight batch budget, and per-peer token bucket throttling (`TIGHTROPE_SYNC_MAX_WIRE_BATCH_BYTES`, `TIGHTROPE_SYNC_MAX_WIRE_BATCH_ENTRIES`, `TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BATCHES`, `TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BYTES`, `TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BYTES_PER_PEER`, `TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BATCHES_PER_PEER`, `TIGHTROPE_SYNC_PEER_RATE_LIMIT_ENTRIES_PER_SECOND`, `TIGHTROPE_SYNC_PEER_RATE_LIMIT_BURST_ENTRIES`) + `RpcIngressQueue` bounded decode/queue + `ReplicationIngressSession` handshake/queue drain + `ReplicationSocketServer` read-pause/drain loop started by `Bridge::cluster_enable` | `native/tests/integration/sync/sync_engine_test.cpp`, `native/tests/unit/sync/rpc_channel_test.cpp`, `native/tests/integration/sync/replication_ingress_test.cpp`, `native/tests/integration/sync/replication_socket_server_test.cpp`, `native/tests/bridge/bridge_lifecycle_test.cpp` |
| **P6-4 / P6-5** dead-peer eviction + lag metrics | Partial (baseline++) | `ClusterStatus` emits per-peer `replication_lag_entries` + heartbeat-failure counts, per-peer ingress apply/replication-latency trends, and per-peer in-flight wire fairness/concurrency (`current/peak` batches/bytes); stale peers transition to disconnected/unreachable and can be auto-evicted via tunable thresholds; cluster-level lag summary/alert telemetry now tracks lagging-peer count + total/max/avg/ewma lag and sustained alert state (`TIGHTROPE_SYNC_REPLICATION_LAG_ALERT_ENTRIES`, `TIGHTROPE_SYNC_REPLICATION_LAG_ALERT_SUSTAINED_REFRESHES`) | `native/tests/bridge/bridge_lifecycle_test.cpp`, `native/tests/integration/sync/sync_engine_test.cpp` |
| **P6-6** protocol version handling | Partial (live ingress + lifecycle probes + fail-closed controls + telemetry surfaced) | Strict reject + optional downgrade negotiation + wire-apply gate + handshake-first socket ingress enforcement path + outbound handshake ACK probe validation path (including timed lifecycle probes) | `native/tests/unit/sync/sync_protocol_test.cpp`, `native/tests/integration/sync/sync_engine_test.cpp`, `native/tests/integration/sync/replication_socket_server_test.cpp`, `native/tests/integration/sync/peer_probe_test.cpp` |

### P6-1: No Authentication -- Any Peer Can Join

**File:** `native/sync/include/transport/tls_stream.h`

The handshake protocol (`sync_protocol.h`) contains `site_id`, `schema_version`, and `last_recv_seq_from_peer` but **no authentication token or certificate verification**. Any device on the network that discovers the mDNS service can join the cluster and inject data.

**Fix:** Add HMAC-SHA256 authentication in the handshake using a pre-shared cluster secret:

```cpp
struct HandshakeFrame {
    std::uint32_t site_id;
    std::uint32_t schema_version;
    std::uint64_t last_recv_seq_from_peer;
    std::array<uint8_t, 32> hmac;  // HMAC-SHA256 over (site_id || schema_version || nonce)
};
```

**Implementation update (2026-03-31):** Handshake authentication is now enforced on the live inbound transport path:
- `ReplicationIngressSession` requires an initial handshake frame before replication frames when configured (`require_initial_handshake=true`)
- handshake HMAC validation uses `validate_handshake_auth` against cluster shared secret before any apply-path work
- socket-level coverage now includes handshake-first enforcement (`native/tests/integration/sync/replication_socket_server_test.cpp`, `native/tests/integration/sync/replication_ingress_test.cpp`)
- inbound listener now sends explicit handshake ACK frames on acceptance, and outbound `peer_probe` validates that ACK (`native/sync/src/transport/peer_probe.cpp`, `native/tests/integration/sync/peer_probe_test.cpp`)
- ingress telemetry now records categorized rejection reasons/counters per peer (including handshake auth/schema rejects) so failed handshakes are visible in cluster status/UI

---

### P6-2: TLS Context Not Initialized

**File:** `native/sync/include/transport/tls_stream.h`

The `TlsStream` class wraps a TCP socket but there is no visible TLS context initialization, certificate loading, or peer verification. It may be a raw TCP connection masquerading as TLS.

**Fix:** Initialize SSL context with certificates, enable peer verification, pin certificates for known peers.

**Implementation update (2026-03-31):** TLS mode is now bound to live cluster ingress listener configuration:
- `ReplicationSocketServer` supports TCP or TLS listener mode and runs live socket ingest in that mode
- `Bridge::cluster_enable` now maps listener TLS mode directly from cluster TLS config (no longer gated by peer-list presence)
- bridge transport tests confirm plaintext ingress applies when TLS is disabled and is blocked when TLS is enabled (`native/tests/bridge/bridge_lifecycle_test.cpp`, `[bridge][lifecycle][cluster][transport]`)
- outbound probe baseline added: `peer_probe` can perform TLS connect+handshake probe with handshake ACK validation and bounded timeout
- `Bridge::refresh_cluster_peers` now runs lifecycle transport probes by default (throttled via `TIGHTROPE_SYNC_PEER_PROBE_{ENABLED,INTERVAL_MS,TIMEOUT_MS,MAX_PER_REFRESH}`), and folds probe failures into peer liveness state
- fail-closed policy controls added (`TIGHTROPE_SYNC_PEER_PROBE_FAIL_CLOSED`, `TIGHTROPE_SYNC_PEER_PROBE_FAIL_CLOSED_FAILURES`) so repeated probe failures can force `unreachable` state and leader-side eviction
- strict fail-closed enforcement added: contradictory config (`peer_probe_fail_closed=true` with `peer_probe_enabled=false`) is rejected at cluster enable, and env-level overrides now force probes on when fail-closed is active
- socket/listener telemetry on live ingress (`ReplicationSocketServer::telemetry()`) now includes: accept failures, accepted/completed/failed connection counts, active/peak connections, TLS handshake/read/apply/handshake-ack failure counters, ingress bytes read, connection duration totals/max/last + histogram buckets (<=10/<=50/<=250/<=1000/>1000ms), queue saturation highs (buffered bytes / queued frames / queued payload bytes), pause cycle/sleep counters, and last socket failure details; surfaced through bridge cluster status and renderer settings
- `Bridge::cluster_add_peer` can still enforce strict add-time preflight via `TIGHTROPE_SYNC_PROBE_PEER_ON_ADD=1`

---

### P6-3: No Rate Limiting or Backpressure

No rate limiting exists anywhere in the replication path. A malicious or malfunctioning peer could flood the node with journal batches.

**Fix:** Add:
- Token bucket rate limiter per peer
- Max in-flight batch count
- Connection-level backpressure (stop reading when apply queue is full)

**Implementation update (2026-03-31):** A hardened baseline ingress guardrail layer now exists in `SyncEngine::apply_wire_batch`:
- hard limit on incoming wire payload size (`TIGHTROPE_SYNC_MAX_WIRE_BATCH_BYTES`, default 4 MiB)
- hard limit on decoded batch entry count (`TIGHTROPE_SYNC_MAX_WIRE_BATCH_ENTRIES`, default 5000)
- max concurrent in-flight wire applies (`TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BATCHES`, default 8)
- global in-flight wire-byte budget (`TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BYTES`, default 16 MiB)
- per-peer in-flight wire-byte budget (`TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BYTES_PER_PEER`, default 8 MiB)
- per-peer in-flight batch budget (`TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BATCHES_PER_PEER`, default 4)
- per-peer token bucket throttling by entries (`TIGHTROPE_SYNC_PEER_RATE_LIMIT_ENTRIES_PER_SECOND`, default 2000; `TIGHTROPE_SYNC_PEER_RATE_LIMIT_BURST_ENTRIES`, default 4000)

Coverage in `native/tests/integration/sync/sync_engine_test.cpp` includes oversized payload rejection, entry-count rejection, global/per-peer in-flight wire-byte budget rejection, and repeated-batch rate-limit rejection.

Transport + runtime update: `RpcIngressQueue` in `native/sync/src/transport/rpc_channel.cpp` now adds bounded ingress buffering/queueing, oversized-frame rejection, and explicit `should_pause_reads()/should_resume_reads()` signals. `ReplicationIngressSession` (`native/sync/src/transport/replication_ingress.cpp`) now supports required handshake-first ingress and bounded frame draining, and `ReplicationSocketServer` (`native/sync/src/transport/replication_socket_server.cpp`) runs a live TCP/TLS socket read loop that pauses new reads while draining queued frames before resuming. `Bridge::cluster_enable` starts this listener on `sync_port`, and `cluster_disable` stops it.

Remaining follow-up for P6-3 is policy hardening, not missing primitives: explicit multi-connection fairness scheduling/attribution beyond current per-peer in-flight current/peak counters.

---

### P6-4: No Dead Peer Eviction

When a peer goes permanently offline, it stays in the membership forever. The leader continues trying to replicate to it, wasting resources and potentially blocking quorum if enough peers are dead.

**Fix:** Implement a dead-peer detector:
- Track consecutive heartbeat failures per peer
- After N failures (configurable, e.g., 100), trigger automatic removal via Raft membership change
- Log the eviction prominently

**Implementation update (2026-03-31):** Baseline dead-peer handling now exists in `Bridge::refresh_cluster_peers`:
- heartbeat-failure counters are derived from last-seen timestamps and lifecycle probe failures (`TIGHTROPE_SYNC_HEARTBEAT_INTERVAL_MS`, `TIGHTROPE_SYNC_DEAD_PEER_DISCONNECT_FAILURES`, `TIGHTROPE_SYNC_DEAD_PEER_UNREACHABLE_FAILURES`)
- unreachable peers can be auto-evicted when local node is leader (`TIGHTROPE_SYNC_DEAD_PEER_EVICTION_FAILURES`, `TIGHTROPE_SYNC_DEAD_PEER_EVICTION_COOLDOWN_MS`)
- fail-closed probe policy can independently trigger eviction when probe-failure threshold is reached
- evictions are logged as `dead_peer_evicted`; failures as `dead_peer_eviction_failed`

Coverage: `native/tests/bridge/bridge_lifecycle_test.cpp` (`[bridge][lifecycle][cluster][eviction]`).

---

### P6-5: No Replication Lag Monitoring

There are no metrics for:
- Per-peer replication lag (leader commit index - follower match index)
- Journal sync latency
- Batch apply duration

**Fix:** Add an observable metrics interface. At minimum, expose lag in `ClusterStatus::peers[].match_index` (already in the struct) and add a `replication_lag_entries` field.

**Implementation update (2026-04-01):** Baseline lag observability is now wired through bridge -> N-API -> renderer:
- per-peer `match_index`, `replication_lag_entries`, `consecutive_heartbeat_failures`, `consecutive_probe_failures`, `last_probe_at`, `last_probe_duration_ms`, and `last_probe_error` now populate `ClusterStatus::peers[]`
- ingress telemetry now surfaces per-peer `ingress_accepted_batches`, `ingress_rejected_batches`, `ingress_accepted_wire_bytes`, `ingress_rejected_wire_bytes`, `ingress_last_wire_batch_bytes`, categorized rejection counters (`batch_too_large`, `backpressure`, `inflight_wire_budget`, `handshake_auth`, `handshake_schema`, `invalid_wire_batch`, `entry_limit`, `rate_limit`, `apply_batch`, `ingress_protocol`), `last_ingress_rejection_reason`, `last_ingress_rejection_at`, and `last_ingress_rejection_error`
- per-peer ingress apply-latency trend telemetry now surfaces `ingress_{total,last,max}_apply_duration_ms`, `ingress_apply_duration_ewma_ms`, and `ingress_apply_duration_samples` (from `SyncEngine` -> bridge -> N-API -> renderer)
- per-peer end-to-end replication-latency trend telemetry now surfaces `ingress_{total,last,max}_replication_latency_ms`, `ingress_replication_latency_ewma_ms`, and `ingress_replication_latency_samples` (computed from decoded wire-entry `hlc_wall` to local apply-time observation)
- per-peer in-flight wire fairness/concurrency telemetry now surfaces `ingress_inflight_wire_{batches,batches_peak,bytes,bytes_peak}` (from `SyncEngine` in-flight budget state)
- cluster-level lag summary + sustained alert telemetry now surfaces `replication_lag_{lagging_peers,total_entries,max_entries,avg_entries,ewma_entries,ewma_samples,alert_threshold_entries,alert_sustained_refreshes,alert_streak,alert_active,last_alert_at}` in `ClusterStatus`
- cluster-level ingress socket telemetry now surfaces `ingress_socket_{accept_failures,accepted_connections,completed_connections,failed_connections,tls_handshake_failures,read_failures,apply_failures,handshake_ack_failures,bytes_read,last_connection_at,last_failure_at,last_failure_error}`
- `SyncEngine` tracks peer ingress telemetry (`last_seen_unix_ms`, `last_reported_seq_from_peer`, acceptance/rejection counters, apply-duration trend stats, replication-latency trend stats) for lag/heartbeat status inputs
- settings UI now shows per-peer state, lag, rejection/failure counters, apply-latency trends, replication-latency trends, in-flight wire fairness/concurrency, and cluster lag summary/alert state

Coverage: `native/tests/integration/sync/sync_engine_test.cpp`, `native/tests/integration/sync/replication_socket_server_test.cpp` + `native/tests/bridge/bridge_lifecycle_test.cpp`; renderer type/unit checks pass (`npm --prefix app run typecheck`, `npm --prefix app run test:unit`).

---

### P6-6: No Protocol Version Evolution

**File:** `native/sync/src/sync_protocol.cpp`

`schema_version` is exchanged in the handshake but there is no logic to handle version mismatches. If nodes run different protocol versions, behavior is undefined.

**Fix:** On version mismatch, either negotiate down to the lowest common version or reject the connection with a clear error.

**Implementation update (2026-03-31):** Protocol version handling now executes at the live socket ingress boundary as part of handshake-first flow:
- unsupported peer schema versions are rejected before replication frames are applied
- downgrade-compatible handshakes are accepted when `allow_schema_downgrade=true`
- socket-level coverage added for strict reject and downgrade acceptance (`native/tests/integration/sync/replication_socket_server_test.cpp`)
- outbound path now has a corresponding probe baseline: handshake ACK is only emitted after auth/schema validation, so `peer_probe` observes version rejection on mismatched peers; lifecycle probes in `Bridge::refresh_cluster_peers` now reuse this path (`native/tests/integration/sync/peer_probe_test.cpp`, `native/tests/bridge/bridge_lifecycle_test.cpp`)

---

## P7 - Memory Management & Resource Leaks

### P7-1: Intentional Static Leaks (18 instances)

The codebase uses a pattern of `static auto* x = new T()` to avoid static destruction order issues. While this is a known C++ idiom, it creates 18 permanent leaks:

| File | Count | What's Leaked |
|---|---|---|
| `native/core/logging/src/logger.cpp` | 2 | LogObserver, mutex |
| `native/auth/oauth/src/oauth_service.cpp` | 4 | mutex, OAuthState, ProviderClient, jthread |
| `native/db/connection/src/sqlite_registry.cpp` | 2 | mutex, unordered_map |
| `native/proxy/src/account_traffic.cpp` | 3 | mutex, unordered_map, callback |
| `native/proxy/session/src/http_bridge.cpp` | 1 | mutex |
| `native/usage/src/usage_fetcher.cpp` | 4 | validator, fetcher, mutex, cache |
| `native/auth/oauth/src/token_refresh.cpp` | 1 | ProviderClient |

**Impact:** These never get freed, but they're process-lifetime objects so the OS reclaims them. The real issue is that asan/msan/valgrind will report them as leaks, making it harder to find real leaks.

**Fix:** Use Meyers singletons or `absl::NoDestructor` to suppress leak warnings while maintaining the same lifetime guarantees:

```cpp
static auto& mutex() {
    static std::mutex instance;
    return instance;
}
```

Or if destruction order truly matters, use `absl::NoDestructor<std::mutex>`.

---

### P7-2: Raw `delete` in Backend Destructor

**File:** `native/sync/src/consensus/nuraft_backend.cpp:55-58`

```cpp
Backend::~Backend() {
    stop();
    delete impl_;
    impl_ = nullptr;
}
```

And in the constructor:
```cpp
Backend::Backend(...) : impl_(new Impl(...)) {}
```

Raw `new`/`delete` is error-prone. If `stop()` throws (it won't due to noexcept, but the pattern is fragile), `impl_` leaks.

**Fix:** Use `std::unique_ptr<Impl>`:

```cpp
class Backend {
    std::unique_ptr<Impl> impl_;
};
```

---

## P8 - Error Handling & Validation

### P8-1: Silently Ignored Database Return Values

**File:** `native/db/repositories/src/api_key_repo_limits.cpp:48,65`

```cpp
(void)delete_stmt.exec();  // Return value explicitly discarded
(void)insert_stmt.exec();  // Return value explicitly discarded
```

If these database operations fail, the code continues as if nothing happened. Data may be inconsistent.

**Fix:** Check return values. Log failures. Propagate errors to callers.

---

### P8-2: Unsafe `reinterpret_cast` for SQLite Text

**Files:**
- `native/auth/oauth/src/token_refresh.cpp:68,71`
- `native/sync/src/persistent_journal.cpp:17`

```cpp
record.refresh_token = reinterpret_cast<const char*>(refresh_raw);
```

`sqlite3_column_text()` returns `const unsigned char*`. A `reinterpret_cast` to `const char*` works on all practical platforms but is technically implementation-defined behavior.

**Fix:** Use a safe conversion wrapper:

```cpp
inline std::string sqlite_text_to_string(const unsigned char* text) {
    return text ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}
```

(The `persistent_journal.cpp` helper `column_text()` already does this correctly -- use it everywhere.)

---

### P8-3: Rollback Failure Silently Ignored

**File:** `native/sync/src/consensus/nuraft_backend_storage.cpp:113,119,123,231`

```cpp
(void)exec_locked("ROLLBACK;");
```

If a ROLLBACK fails (e.g., because the connection is broken), the database may be in an inconsistent state. The `(void)` cast explicitly suppresses the warning.

**Fix:** At minimum, log the rollback failure. Consider closing and reopening the connection if rollback fails.

---

### P8-4: No Password Hash Null-Termination Validation

**File:** `native/auth/dashboard/src/password_auth.cpp:38-42`

```cpp
return crypto_pwhash_str_verify(
    std::string(password_hash).c_str(),  // creates temporary string just for c_str()
    password.data(),
    static_cast<unsigned long long>(password.size())
) == 0;
```

Constructing a temporary `std::string` from `string_view` just to get `c_str()` is wasteful. More importantly, if `password_hash` is not null-terminated (it's a `string_view`), this creates a copy to ensure null termination -- which is correct but should be documented.

**Fix:** Assert or validate that the hash has the expected argon2 format before passing to libsodium.

---

## P9 - Hardcoded Config & Missing Tunables

### P9-1: Raft Timeouts Not Configurable

**File:** `native/sync/src/consensus/nuraft_backend.cpp:92-97`

```cpp
params.with_election_timeout_lower(150)
    .with_election_timeout_upper(350)
    .with_hb_interval(75)
    .with_rpc_failure_backoff(50)
    .with_max_append_size(64);
```

These values are reasonable for LAN but wrong for WAN. No way to tune them without recompiling.

**Fix:** Accept these as part of `ClusterConfig`.

---

### P9-2: Sticky Session TTL Hardcoded

**File:** `native/proxy/session/src/sticky_affinity.cpp:42-43`

```cpp
constexpr std::int64_t kStickyTtlMs = 30 * 60 * 1000;      // 30 minutes
constexpr std::int64_t kCleanupIntervalMs = 60 * 1000;       // 1 minute
```

**Fix:** Move to `Config` struct or settings database.

---

### P9-3: NuRaft Thread Pool Size Hardcoded

**File:** `native/sync/src/consensus/nuraft_backend.cpp:100`

```cpp
asio_options.thread_pool_size_ = 2;
```

On a 64-core machine this wastes resources. On a 1-core machine this oversubscribes.

**Fix:** Default to `std::min(std::thread::hardware_concurrency(), 4u)` or make configurable.

---

### P9-4: Default Bind Address is Localhost Only

**File:** `native/bridge/include/bridge.h:19`

```cpp
std::string host = "127.0.0.1";
```

For cluster mode to work, nodes need to bind to a routable address. The default of `127.0.0.1` means cluster mode cannot work without explicit config override.

**Fix:** When cluster mode is enabled, validate that `host` is not localhost, or auto-detect a routable address.

---

## P10 - Test Coverage Gaps

### Missing Test Categories

| Category | Current Coverage | What's Missing |
|---|---|---|
| **Raft failure scenarios** | 0 tests | Leader crash + recovery, follower crash, split-brain simulation |
| **Network partition** | 0 tests | Minority/majority partition behavior, healing |
| **Concurrent operations** | 0 tests | Parallel proposals, concurrent reads/writes, session manager thread safety |
| **Crash recovery** | Added baseline startup-schema recovery coverage | Startup integrity check invocation + malformed sync-metadata recovery/deny modes are now covered; kill-mid-write restart scenarios are still missing |
| **Full disk handling** | 0 tests | SQLite IOERR behavior, graceful degradation |
| **Protocol version mismatch** | Added baseline coverage | Unit + integration tests now cover strict reject and downgrade-allowed negotiation; broader mixed-version cluster scenarios still missing |
| **Security** | Added baseline coverage | Handshake HMAC validation and TLS verification guardrails now have unit/integration tests; unauthorized live peer and end-to-end cert-chain coverage still missing |
| **Stress / load** | 0 tests | Large log sizes, slow followers, high message loss |
| **Bridge error paths** | 0 tests | Native module failure propagation to JS |
| **Proxy tool-call argument replay** | Added regression coverage | `delta -> done -> output_item.done` no longer duplicates arguments in `/v1/chat/completions` |
| **Clock.h / time mocking** | Added baseline support | `SystemClock` + `ManualClock` now exist with tests; adoption into broader runtime paths is still incomplete |

### Existing Tests That Pass But Are Still Insufficient

- `raft_node_test.cpp` now covers deterministic storage/persistence basics, but still lacks multi-node crash/recovery and partition scenarios.
- `membership_test.cpp` mostly exercises local state transitions, not fully replicated membership reconfiguration.
- `leader_proxy_test.cpp` still validates decision logic only; no end-to-end forwarding with live consensus leadership changes.

---

## Summary Scorecard

| Module | Grade | Blocking Issues | Notes |
|---|---|---|---|
| **Proxy/Streaming** | B+ | None | Solid, well-structured; added regression guard for tool-call argument replay duplication |
| **Balancer** | B+ | None critical | Thread-safety races fixed; still room for contention/perf tuning |
| **Auth (Dashboard/OAuth)** | B | None critical | Session manager mutex + password hash validation landed |
| **Auth (Crypto)** | C- | Runtime integration | Crypto/key primitives exist, but secret persistence paths are not yet using them end-to-end |
| **Database/Repositories** | B+ | None critical | Explicit DB return/error handling improved |
| **Bridge (N-API)** | B | P3-5 | Error propagation, degraded status, timeout, and input validation fixed |
| **Consensus (Raft)** | B- | P4-4 hardening | Restart durability, test-mode, membership plumbing, and leader-only linearizable read gating landed; full ReadIndex/forwarding strategy is still open |
| **Journaling** | A- | None critical | Durability, rollback handling, compaction, reclaim, local-read checksum verification, and startup sync-metadata corruption recovery guardrails are in place |
| **Replication** | B | P6 transport hardening | Inbound socket backpressure/rate limiting, default-on lifecycle peer transport probing, per-peer ingress apply/replication-latency + in-flight fairness telemetry, and cluster sustained lag summary/alert state are now wired into live runtime status; strict fail-closed policy tuning, mixed-version interop depth, and exported alert hooks are still open |
| **Build System** | B+ | None | Well-organized, cross-platform |
| **Test Coverage** | C- | P10 | Better guardrails now, but failure/partition/load coverage remains thin |

### Priority Order for Remaining Fixes (Synced)

1. **P6-1 / P6-2 / P6-6** -- Harden lifecycle probe baseline further (interop/fail-closed policy tuning + richer transport attribution)
2. **P1** -- Remaining rollout/cleanup items (utility adoption polish + pricing freshness ops cadence)
3. **P6-4 / P6-5** -- Externalize sustained lag alerts (hooks/runbook integration) and close remaining liveness policy gaps
4. **P4-4** -- Extend from leader-only read rejection to full ReadIndex/forwarding strategy
5. **P10** -- Partition/crash/load/security test matrix expansion

### Remaining LOC Estimate (Synced)

| Priority | Estimated LOC | Effort |
|---|---|---|
| P1 remaining rollout + cleanup | ~35 | <1 day |
| P4 consensus gaps | ~120 | <1 day |
| P6 replication/security gaps | ~240 | 1-2 days |
| P10 failure/load/security tests | ~1800 | 5-7 days |
| **Total remaining** | **~2195** | **~2-3 weeks** |

---

*Generated by brutal senior C++ engineer audit. No feelings were spared.*
