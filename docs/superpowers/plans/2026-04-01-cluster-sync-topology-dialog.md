# Cluster Sync Topology Dialog — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a real-time cluster sync topology dialog driven by fine-grained push events from the native C++ sync engine, with zero renderer polling.

**Architecture:** A singleton C++ `SyncEventEmitter` holds one N-API ThreadSafeFunction. Seven event types (journal_entry, peer_state_change, role_change, commit_advance, term_change, ingress_batch, lag_alert) are emitted from the sync engine, serialized to JS objects on the Node.js thread, forwarded through the Electron IPC stack to the renderer, and used to surgically patch a local `ClusterStatus` state in the `useSyncTopology` hook. The `SyncTopologyDialog` component reads from that hook and re-renders on every patch.

**Tech Stack:** C++20, N-API / node-addon-api (`napi.h`), TypeScript, React 19, Vitest, @testing-library/react

---

## Spec

`docs/superpowers/specs/2026-04-01-cluster-sync-topology-dialog-design.md`

---

## File Map

| File | Status | Responsibility |
|---|---|---|
| `native/sync/include/sync_event_emitter.h` | New | Event struct types, variant, emitter class interface |
| `native/sync/src/sync_event_emitter.cpp` | New | TSFN lifecycle, emit(), JS serializer |
| `native/sync/src/journal.cpp` | Modify | Emit `journal_entry` from `Journal::append()` |
| `native/sync/src/consensus/nuraft_backend.cpp` | Modify | Emit `role_change`, `term_change`, `commit_advance` from nuraft callbacks |
| `native/bridge/src/bridge.cpp` | Modify | Emit `peer_state_change` and `lag_alert` |
| `native/sync/src/transport/replication_ingress.cpp` | Modify | Emit `ingress_batch` after wire batch apply |
| `native/bridge/src/addon.cpp` | Modify | Expose `registerSyncEventCallback`, `unregisterSyncEventCallback` |
| `app/src/main/native.ts` | Modify | Add `SyncEvent` discriminated union, two new `NativeModule` methods + stubs |
| `app/src/main/index.ts` | Modify | Register callback after window creation, forward via `webContents.send` |
| `app/src/preload/index.ts` | Modify | Expose `onSyncEvent(listener) => unsubscribe` |
| `app/src/renderer/shared/types.ts` | Modify | Add renderer-side `SyncEvent` union, add `onSyncEvent` to `ElectronApi` |
| `app/src/renderer/test/setup.ts` | Modify | Add `onSyncEvent` stub to `window.tightrope` mock |
| `app/src/renderer/state/useSyncTopology.ts` | New | Hook: seed + subscribe + patch `ClusterStatus` |
| `app/src/renderer/state/useSyncTopology.test.ts` | New | Vitest unit tests for the hook |
| `app/src/renderer/components/dialogs/SyncTopologyDialog.tsx` | New | Visual topology dialog (leader + follower cards, SVG connections) |
| `app/src/renderer/components/dialogs/SyncTopologyDialog.test.tsx` | New | RTL unit tests for the dialog |
| `app/src/renderer/shared/types.ts` | Modify | Add `syncTopologyDialogOpen` to `AppRuntimeState` |
| `app/src/renderer/data/seed.ts` | Modify | Add `syncTopologyDialogOpen: false` to `createInitialRuntimeState()` |
| `app/src/renderer/state/useTightropeState.ts` | Modify | Add `openSyncTopologyDialog` / `closeSyncTopologyDialog` |
| `app/src/renderer/components/settings/SettingsPage.tsx` | Modify | Add `onOpenSyncTopology` prop, thread to `DatabaseSyncSection` |
| `app/src/renderer/components/settings/sections/DatabaseSyncSection.tsx` | Modify | Add `onOpenSyncTopology` prop + "View topology" button |
| `app/src/renderer/App.tsx` | Modify | Mount `SyncTopologyDialog`, call `useSyncTopology`, wire open state |

---

## Task 1: SyncEventEmitter header

**Files:**
- Create: `native/sync/include/sync_event_emitter.h`

- [ ] **Step 1: Create the header**

```cpp
#pragma once
// Thread-safe single-callback event emitter for sync engine events.
// Any C++ thread calls SyncEventEmitter::get().emit(event).
// Requires register_callback() before events are delivered.

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <variant>

#include <napi.h>

namespace tightrope::sync {

struct SyncEventJournalEntry {
    std::uint64_t seq = 0;
    std::string table;
    std::string op;
};

struct SyncEventPeerStateChange {
    std::string site_id;
    std::string state; // "connected" | "disconnected" | "unreachable"
    std::string address;
};

struct SyncEventRoleChange {
    std::string role;      // "leader" | "follower" | "candidate"
    std::uint64_t term = 0;
    std::string leader_id; // empty string serialises as null
};

struct SyncEventCommitAdvance {
    std::uint64_t commit_index = 0;
    std::uint64_t last_applied = 0;
};

struct SyncEventTermChange {
    std::uint64_t term = 0;
};

struct SyncEventIngressBatch {
    std::string site_id;
    bool accepted = false;
    std::uint64_t bytes = 0;
    std::uint64_t apply_duration_ms = 0;
    std::uint64_t replication_latency_ms = 0;
};

struct SyncEventLagAlert {
    bool active = false;
    std::uint32_t lagging_peers = 0;
    std::uint64_t max_lag = 0;
};

using SyncEvent = std::variant<
    SyncEventJournalEntry,
    SyncEventPeerStateChange,
    SyncEventRoleChange,
    SyncEventCommitAdvance,
    SyncEventTermChange,
    SyncEventIngressBatch,
    SyncEventLagAlert
>;

class SyncEventEmitter {
public:
    static SyncEventEmitter& get() noexcept;

    // Call from Node.js thread (addon init / registerSyncEventCallback).
    void register_callback(Napi::Env env, Napi::Function fn);

    // Call from Node.js thread (addon shutdown / unregisterSyncEventCallback).
    void unregister_callback();

    // Safe to call from any C++ thread. No-op if not registered.
    void emit(SyncEvent event) noexcept;

private:
    SyncEventEmitter() = default;
    ~SyncEventEmitter();

    static Napi::Object serialize(Napi::Env env, const SyncEvent& event);

    std::mutex mutex_;
    Napi::ThreadSafeFunction tsfn_;
    bool active_ = false;
};

} // namespace tightrope::sync
```

---

## Task 2: SyncEventEmitter implementation

**Files:**
- Create: `native/sync/src/sync_event_emitter.cpp`

- [ ] **Step 1: Write the implementation**

```cpp
#include "sync_event_emitter.h"

#include <chrono>
#include <type_traits>

namespace tightrope::sync {

namespace {

std::uint64_t unix_ms_now() noexcept {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
    );
}

} // namespace

SyncEventEmitter& SyncEventEmitter::get() noexcept {
    static SyncEventEmitter instance;
    return instance;
}

SyncEventEmitter::~SyncEventEmitter() {
    unregister_callback();
}

void SyncEventEmitter::register_callback(Napi::Env env, Napi::Function fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) {
        tsfn_.Release();
        active_ = false;
    }
    tsfn_ = Napi::ThreadSafeFunction::New(
        env,
        fn,
        "SyncEventEmitter", // resource name for debugging
        0,                   // unlimited queue depth
        1                    // initial thread count
    );
    active_ = true;
}

void SyncEventEmitter::unregister_callback() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) {
        tsfn_.Release();
        active_ = false;
    }
}

void SyncEventEmitter::emit(SyncEvent event) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) return;

    const std::uint64_t ts = unix_ms_now();
    auto envelope = std::make_shared<std::pair<std::uint64_t, SyncEvent>>(ts, std::move(event));

    const auto status = tsfn_.NonBlockingCall(
        [envelope](Napi::Env env, Napi::Function fn) {
            Napi::Object obj = SyncEventEmitter::serialize(env, envelope->second);
            obj.Set("ts", Napi::Number::New(env, static_cast<double>(envelope->first)));
            fn.Call({obj});
        }
    );

    if (status == napi_closing) {
        active_ = false;
    }
}

Napi::Object SyncEventEmitter::serialize(Napi::Env env, const SyncEvent& event) {
    auto obj = Napi::Object::New(env);

    std::visit([&](const auto& e) {
        using T = std::decay_t<decltype(e)>;

        if constexpr (std::is_same_v<T, SyncEventJournalEntry>) {
            obj.Set("type", Napi::String::New(env, "journal_entry"));
            obj.Set("seq", Napi::Number::New(env, static_cast<double>(e.seq)));
            obj.Set("table", Napi::String::New(env, e.table));
            obj.Set("op", Napi::String::New(env, e.op));

        } else if constexpr (std::is_same_v<T, SyncEventPeerStateChange>) {
            obj.Set("type", Napi::String::New(env, "peer_state_change"));
            obj.Set("site_id", Napi::String::New(env, e.site_id));
            obj.Set("state", Napi::String::New(env, e.state));
            obj.Set("address", Napi::String::New(env, e.address));

        } else if constexpr (std::is_same_v<T, SyncEventRoleChange>) {
            obj.Set("type", Napi::String::New(env, "role_change"));
            obj.Set("role", Napi::String::New(env, e.role));
            obj.Set("term", Napi::Number::New(env, static_cast<double>(e.term)));
            if (e.leader_id.empty()) {
                obj.Set("leader_id", env.Null());
            } else {
                obj.Set("leader_id", Napi::String::New(env, e.leader_id));
            }

        } else if constexpr (std::is_same_v<T, SyncEventCommitAdvance>) {
            obj.Set("type", Napi::String::New(env, "commit_advance"));
            obj.Set("commit_index", Napi::Number::New(env, static_cast<double>(e.commit_index)));
            obj.Set("last_applied", Napi::Number::New(env, static_cast<double>(e.last_applied)));

        } else if constexpr (std::is_same_v<T, SyncEventTermChange>) {
            obj.Set("type", Napi::String::New(env, "term_change"));
            obj.Set("term", Napi::Number::New(env, static_cast<double>(e.term)));

        } else if constexpr (std::is_same_v<T, SyncEventIngressBatch>) {
            obj.Set("type", Napi::String::New(env, "ingress_batch"));
            obj.Set("site_id", Napi::String::New(env, e.site_id));
            obj.Set("accepted", Napi::Boolean::New(env, e.accepted));
            obj.Set("bytes", Napi::Number::New(env, static_cast<double>(e.bytes)));
            obj.Set("apply_duration_ms", Napi::Number::New(env, static_cast<double>(e.apply_duration_ms)));
            obj.Set("replication_latency_ms", Napi::Number::New(env, static_cast<double>(e.replication_latency_ms)));

        } else if constexpr (std::is_same_v<T, SyncEventLagAlert>) {
            obj.Set("type", Napi::String::New(env, "lag_alert"));
            obj.Set("active", Napi::Boolean::New(env, e.active));
            obj.Set("lagging_peers", Napi::Number::New(env, static_cast<double>(e.lagging_peers)));
            obj.Set("max_lag", Napi::Number::New(env, static_cast<double>(e.max_lag)));
        }
    }, event);

    return obj;
}

} // namespace tightrope::sync
```

- [ ] **Step 2: Verify the build picks up the new file**

The `CMakeLists.txt` uses `file(GLOB_RECURSE ... native/sync/*.cpp)` so no changes are needed. Verify by running a build:
```
node app/scripts/native-module.js --force
```
Expected: build completes without errors about `sync_event_emitter`.

---

## Task 3: C++ emission call sites

**Files:**
- Modify: `native/sync/src/journal.cpp`
- Modify: `native/sync/src/consensus/nuraft_backend.cpp`
- Modify: `native/bridge/src/bridge.cpp`
- Modify: `native/sync/src/transport/replication_ingress.cpp`

### 3a — journal_entry

- [ ] **Step 1: Add include and emit to `Journal::append()`**

Open `native/sync/src/journal.cpp`. At the top, add:
```cpp
#include "sync_event_emitter.h"
```

In `Journal::append()`, immediately before `return entry;`, add:
```cpp
SyncEventEmitter::get().emit(SyncEventJournalEntry{
    .seq   = entry.seq,
    .table = entry.table_name,
    .op    = entry.op,
});
```

### 3b — role_change, term_change, commit_advance

- [ ] **Step 1: Open `native/sync/src/consensus/nuraft_backend.cpp` and locate the state machine callbacks**

This file implements the nuraft `state_machine` interface. Find:
- `commit(const ulong log_idx, ...)` — called when a log entry is committed. Add:
  ```cpp
  SyncEventEmitter::get().emit(SyncEventCommitAdvance{
      .commit_index = static_cast<std::uint64_t>(log_idx),
      .last_applied = static_cast<std::uint64_t>(log_idx),
  });
  ```

- The role/term change callbacks (nuraft calls these when leadership changes). Look for methods named `become_leader`, `become_follower`, `become_candidate`, or a `server_status_change_` / `state_change_` callback. In each, add the appropriate emit. Example for `become_leader(const ulong term)`:
  ```cpp
  SyncEventEmitter::get().emit(SyncEventTermChange{ .term = static_cast<std::uint64_t>(term) });
  SyncEventEmitter::get().emit(SyncEventRoleChange{
      .role      = "leader",
      .term      = static_cast<std::uint64_t>(term),
      .leader_id = "",  // self — use the node_id if available
  });
  ```
  For `become_follower(const ulong term, const ulong leader_id)`:
  ```cpp
  SyncEventEmitter::get().emit(SyncEventTermChange{ .term = static_cast<std::uint64_t>(term) });
  SyncEventEmitter::get().emit(SyncEventRoleChange{
      .role      = "follower",
      .term      = static_cast<std::uint64_t>(term),
      .leader_id = std::to_string(leader_id),
  });
  ```

Add the include at the top:
```cpp
#include "sync_event_emitter.h"
```

### 3c — peer_state_change

The peer state (connected/disconnected/unreachable) is computed in `Bridge::cluster_status()` in `native/bridge/src/bridge.cpp`. The bridge's cluster state struct (`cluster_->`) tracks ongoing state.

- [ ] **Step 1: Add a previous-state map to the bridge cluster state**

In `native/bridge/include/bridge.h`, find the cluster state struct (look for `struct ClusterState` or the member `cluster_`). Add a map to track the last-known peer states:
```cpp
std::unordered_map<std::uint32_t, std::string> prev_peer_states; // site_id -> "connected"|"disconnected"|"unreachable"
```

- [ ] **Step 2: Add the emit in `cluster_status()` in `bridge.cpp`**

Add the include:
```cpp
#include "sync_event_emitter.h"
```

In `cluster_status()`, after computing `item.state` for each peer (around line 1323–1346 where `status.peers.push_back(std::move(item))` is), and before that push, add:

```cpp
// Convert state enum to string once
const std::string state_str =
    item.state == PeerState::Connected    ? "connected"    :
    item.state == PeerState::Disconnected ? "disconnected" : "unreachable";

// Emit only if state changed
const auto site_key = item.site_id_u32; // use whichever uint32 site_id field the item has
auto& prev = cluster_->prev_peer_states[site_key];
if (prev != state_str) {
    prev = state_str;
    SyncEventEmitter::get().emit(SyncEventPeerStateChange{
        .site_id = item.site_id,   // the string site_id on the PeerStatus struct
        .state   = state_str,
        .address = item.address,
    });
}
```

> Note: check the exact field names in the `PeerStatus` struct in `bridge.h` (`site_id` string, the uint32 version, and `address`). Adapt accordingly.

### 3d — ingress_batch

- [ ] **Step 1: Add include and emit in `replication_ingress.cpp`**

Add the include:
```cpp
#include "sync_event_emitter.h"
```

In `ReplicationIngressSession::consume_frames()` (or the path that calls `SyncEngine::apply_wire_batch()`), after the apply completes, add:

```cpp
// After apply_wire_batch call, where result and request_ are available:
const auto peer_site_id = std::to_string(request_.remote_handshake.site_id);
SyncEventEmitter::get().emit(SyncEventIngressBatch{
    .site_id               = peer_site_id,
    .accepted              = result.success,
    .bytes                 = wire_batch_bytes, // size of the wire_batch vector/span
    .apply_duration_ms     = result.apply_duration_ms,     // if available on ApplyBatchResult; else 0
    .replication_latency_ms = 0, // replication latency is tracked by PeerIngressTelemetry; use 0 here
});
```

> `ApplyBatchResult` does not carry apply_duration_ms directly — use 0 or add the field. The per-peer EWMA latency is already tracked in `PeerIngressTelemetry`; this event just signals accept/reject with byte count.

---

## Task 4: lag_alert emission

**Files:**
- Modify: `native/bridge/src/bridge.cpp`

- [ ] **Step 1: Emit lag_alert on transitions in `cluster_status()`**

The lag alert computation is already in `cluster_status()` around lines 1436–1459. Find the two transition points:

**Alert activates** (currently just `cluster_->replication_lag_alert_active = next_alert_active`):
```cpp
if (!cluster_->replication_lag_alert_active && next_alert_active) {
    // existing comment / logging if any
    SyncEventEmitter::get().emit(SyncEventLagAlert{
        .active        = true,
        .lagging_peers = static_cast<std::uint32_t>(lagging_peers),
        .max_lag       = status.replication_lag_max_entries,
    });
}
```

**Alert clears** (around line 1436–1437 where `replication_lag_alert_active = false`):
```cpp
if (cluster_->replication_lag_alert_active && !next_alert_active) {
    SyncEventEmitter::get().emit(SyncEventLagAlert{
        .active        = false,
        .lagging_peers = 0,
        .max_lag       = 0,
    });
}
```

> The emit for "clears" must be added in a guard before setting `cluster_->replication_lag_alert_active = false`. Read the surrounding code carefully to avoid double-emitting.

---

## Task 5: N-API addon binding

**Files:**
- Modify: `native/bridge/src/addon.cpp`

- [ ] **Step 1: Add include**

At the top of `addon.cpp`, add:
```cpp
#include "sync_event_emitter.h"
```

- [ ] **Step 2: Add the two handler functions**

After the `sync_rollback_batch` function (before the `} // namespace` closing brace):

```cpp
Napi::Value register_sync_event_callback(const Napi::CallbackInfo& info) {
    if (info.Length() < 1 || !info[0].IsFunction()) {
        throw Napi::TypeError::New(info.Env(), "registerSyncEventCallback requires a function");
    }
    tightrope::sync::SyncEventEmitter::get().register_callback(
        info.Env(),
        info[0].As<Napi::Function>()
    );
    return info.Env().Undefined();
}

Napi::Value unregister_sync_event_callback(const Napi::CallbackInfo& info) {
    tightrope::sync::SyncEventEmitter::get().unregister_callback();
    return info.Env().Undefined();
}
```

- [ ] **Step 3: Register the functions in `InitAddon`**

In `InitAddon`, after the `syncRollbackBatch` line, add:
```cpp
exports.Set("registerSyncEventCallback",   Napi::Function::New(env, register_sync_event_callback));
exports.Set("unregisterSyncEventCallback", Napi::Function::New(env, unregister_sync_event_callback));
```

- [ ] **Step 4: Build and verify**

```
node app/scripts/native-module.js --force
```
Expected: build succeeds.

---

## Task 6: Main process — SyncEvent types and NativeModule

**Files:**
- Modify: `app/src/main/native.ts`

- [ ] **Step 1: Add the SyncEvent discriminated union**

After the `PeerStatus` interface definition (around line 151), add:

```ts
export type SyncEvent =
  | { type: 'journal_entry';    ts: number; seq: number; table: string; op: string }
  | { type: 'peer_state_change'; ts: number; site_id: string; state: 'connected' | 'disconnected' | 'unreachable'; address: string }
  | { type: 'role_change';      ts: number; role: 'leader' | 'follower' | 'candidate'; term: number; leader_id: string | null }
  | { type: 'commit_advance';   ts: number; commit_index: number; last_applied: number }
  | { type: 'term_change';      ts: number; term: number }
  | { type: 'ingress_batch';    ts: number; site_id: string; accepted: boolean; bytes: number; apply_duration_ms: number; replication_latency_ms: number }
  | { type: 'lag_alert';        ts: number; active: boolean; lagging_peers: number; max_lag: number };
```

- [ ] **Step 2: Add methods to `NativeModule` interface**

In the `NativeModule` interface (around line 153), add:
```ts
registerSyncEventCallback(fn: (event: SyncEvent) => void): void;
unregisterSyncEventCallback(): void;
```

- [ ] **Step 3: Add stubs to `createNativeStubs()`**

In `createNativeStubs()`, add:
```ts
registerSyncEventCallback(_fn: (event: SyncEvent) => void) {},
unregisterSyncEventCallback() {},
```

---

## Task 7: Main process — push events to renderer

**Files:**
- Modify: `app/src/main/index.ts`

- [ ] **Step 1: Register the sync event callback after window creation**

In `index.ts`, the `mainWindow` is created inside `app.whenReady().then(async () => { ... })`. After the `mainWindow` assignment and before `loadRenderer`, add:

```ts
native.registerSyncEventCallback((event) => {
  if (!mainWindow || mainWindow.isDestroyed()) return;
  mainWindow.webContents.send('sync:event', event);
});
```

This follows the same pattern as the existing `emitOauthDeepLink` function in that file.

---

## Task 8: Preload

**Files:**
- Modify: `app/src/preload/index.ts`

- [ ] **Step 1: Add `onSyncEvent` to the context bridge**

In `app/src/preload/index.ts`, add this to `contextBridge.exposeInMainWorld('tightrope', { ... })` after the `rollbackSyncBatch` entry:

```ts
onSyncEvent: (listener: (event: unknown) => void) => {
  const handler = (_event: unknown, payload: unknown) => {
    if (!payload || typeof payload !== 'object') return;
    const type = (payload as { type?: unknown }).type;
    if (typeof type !== 'string') return;
    listener(payload);
  };
  ipcRenderer.on('sync:event', handler);
  return () => ipcRenderer.removeListener('sync:event', handler);
},
```

> The preload does not import `SyncEvent` from main — the type safety lives in the renderer's `ElectronApi` declaration. The handler does a minimal shape check.

---

## Task 9: Renderer types

**Files:**
- Modify: `app/src/renderer/shared/types.ts`
- Modify: `app/src/renderer/test/setup.ts`

- [ ] **Step 1: Add `SyncEvent` to `shared/types.ts`**

After the `ClusterPeerSource` type (around line 16), add:

```ts
export type SyncEvent =
  | { type: 'journal_entry';    ts: number; seq: number; table: string; op: string }
  | { type: 'peer_state_change'; ts: number; site_id: string; state: ClusterPeerState; address: string }
  | { type: 'role_change';      ts: number; role: ClusterRole; term: number; leader_id: string | null }
  | { type: 'commit_advance';   ts: number; commit_index: number; last_applied: number }
  | { type: 'term_change';      ts: number; term: number }
  | { type: 'ingress_batch';    ts: number; site_id: string; accepted: boolean; bytes: number; apply_duration_ms: number; replication_latency_ms: number }
  | { type: 'lag_alert';        ts: number; active: boolean; lagging_peers: number; max_lag: number };
```

- [ ] **Step 2: Add `onSyncEvent` to `ElectronApi`**

In the `ElectronApi` interface (around line 453), add after `onOauthDeepLink`:
```ts
onSyncEvent: (listener: (event: SyncEvent) => void) => () => void;
```

Also add `syncTopologyDialogOpen: boolean` to `AppRuntimeState` (around line 156, after `authDialogOpen`):
```ts
syncTopologyDialogOpen: boolean;
```

- [ ] **Step 3: Add `onSyncEvent` stub to `setup.ts`**

In `app/src/renderer/test/setup.ts`, add to the `window.tightrope` mock (after `onOauthDeepLink`):
```ts
onSyncEvent: () => () => {},
```

---

## Task 10: useSyncTopology hook

**Files:**
- Create: `app/src/renderer/state/useSyncTopology.ts`
- Create: `app/src/renderer/state/useSyncTopology.test.ts`

- [ ] **Step 1: Write the failing tests**

Create `app/src/renderer/state/useSyncTopology.test.ts`:

```ts
import { renderHook, act } from '@testing-library/react';
import { describe, it, expect, vi, beforeEach } from 'vitest';
import type { ClusterStatus, SyncEvent } from '../shared/types';
import { useSyncTopology } from './useSyncTopology';

const emptyStatus: ClusterStatus = {
  enabled: false, site_id: '', cluster_name: '', role: 'standalone', term: 0,
  commit_index: 0, last_applied: 0, leader_id: null, peers: [],
  replication_lagging_peers: 0, replication_lag_total_entries: 0,
  replication_lag_max_entries: 0, replication_lag_avg_entries: 0,
  replication_lag_ewma_entries: 0, replication_lag_ewma_samples: 0,
  replication_lag_alert_threshold_entries: 0, replication_lag_alert_sustained_refreshes: 0,
  replication_lag_alert_streak: 0, replication_lag_alert_active: false,
  replication_lag_last_alert_at: null,
  ingress_socket_accept_failures: 0, ingress_socket_accepted_connections: 0,
  ingress_socket_completed_connections: 0, ingress_socket_failed_connections: 0,
  ingress_socket_active_connections: 0, ingress_socket_peak_active_connections: 0,
  ingress_socket_tls_handshake_failures: 0, ingress_socket_read_failures: 0,
  ingress_socket_apply_failures: 0, ingress_socket_handshake_ack_failures: 0,
  ingress_socket_bytes_read: 0, ingress_socket_total_connection_duration_ms: 0,
  ingress_socket_last_connection_duration_ms: 0, ingress_socket_max_connection_duration_ms: 0,
  ingress_socket_connection_duration_ewma_ms: 0, ingress_socket_connection_duration_le_10ms: 0,
  ingress_socket_connection_duration_le_50ms: 0, ingress_socket_connection_duration_le_250ms: 0,
  ingress_socket_connection_duration_le_1000ms: 0, ingress_socket_connection_duration_gt_1000ms: 0,
  ingress_socket_max_buffered_bytes: 0, ingress_socket_max_queued_frames: 0,
  ingress_socket_max_queued_payload_bytes: 0, ingress_socket_paused_read_cycles: 0,
  ingress_socket_paused_read_sleep_ms: 0, ingress_socket_last_connection_at: null,
  ingress_socket_last_failure_at: null, ingress_socket_last_failure_error: null,
  journal_entries: 0, pending_raft_entries: 0, last_sync_at: null,
};

describe('useSyncTopology', () => {
  let capturedListener: ((event: SyncEvent) => void) | null = null;

  beforeEach(() => {
    capturedListener = null;
    (window.tightrope as unknown as Record<string, unknown>).getClusterStatus =
      vi.fn().mockResolvedValue({ ...emptyStatus, enabled: true, role: 'leader', term: 5, commit_index: 100 });
    (window.tightrope as unknown as Record<string, unknown>).onSyncEvent =
      vi.fn().mockImplementation((listener: (event: SyncEvent) => void) => {
        capturedListener = listener;
        return () => { capturedListener = null; };
      });
  });

  it('returns null status when closed', () => {
    const { result } = renderHook(() => useSyncTopology(false));
    expect(result.current.status).toBeNull();
    expect(result.current.lastEventAt).toBeNull();
  });

  it('seeds status from getClusterStatus when opened', async () => {
    const { result } = renderHook(() => useSyncTopology(true));
    await act(async () => {});
    expect(result.current.status?.role).toBe('leader');
    expect(result.current.status?.term).toBe(5);
    expect(result.current.status?.commit_index).toBe(100);
  });

  it('patches commit_index on commit_advance event', async () => {
    const { result } = renderHook(() => useSyncTopology(true));
    await act(async () => {});
    const event: SyncEvent = { type: 'commit_advance', ts: Date.now(), commit_index: 200, last_applied: 199 };
    act(() => { capturedListener?.(event); });
    expect(result.current.status?.commit_index).toBe(200);
    expect(result.current.lastEventAt).not.toBeNull();
  });

  it('patches journal_entries on journal_entry event', async () => {
    const { result } = renderHook(() => useSyncTopology(true));
    await act(async () => {});
    const before = result.current.status?.journal_entries ?? 0;
    act(() => { capturedListener?.({ type: 'journal_entry', ts: Date.now(), seq: 1, table: 't', op: 'insert' }); });
    expect(result.current.status?.journal_entries).toBe(before + 1);
  });

  it('patches role on role_change event', async () => {
    const { result } = renderHook(() => useSyncTopology(true));
    await act(async () => {});
    act(() => { capturedListener?.({ type: 'role_change', ts: Date.now(), role: 'follower', term: 6, leader_id: '2' }); });
    expect(result.current.status?.role).toBe('follower');
    expect(result.current.status?.term).toBe(6);
    expect(result.current.status?.leader_id).toBe('2');
  });

  it('unsubscribes when closed', async () => {
    const { result, rerender } = renderHook(({ open }) => useSyncTopology(open), {
      initialProps: { open: true },
    });
    await act(async () => {});
    rerender({ open: false });
    expect(result.current.status).toBeNull();
    expect(capturedListener).toBeNull();
  });
});
```

- [ ] **Step 2: Run tests — expect FAIL (module not found)**

```
npm --prefix app run test:unit -- --reporter=verbose useSyncTopology
```
Expected: FAIL with "Cannot find module './useSyncTopology'"

- [ ] **Step 3: Write the hook**

Create `app/src/renderer/state/useSyncTopology.ts`:

```ts
import { useEffect, useRef, useState } from 'react';
import type { ClusterStatus, SyncEvent } from '../shared/types';

export interface SyncTopologyResult {
  status: ClusterStatus | null;
  lastEventAt: number | null;
}

export function useSyncTopology(open: boolean): SyncTopologyResult {
  const [status, setStatus] = useState<ClusterStatus | null>(null);
  const [lastEventAt, setLastEventAt] = useState<number | null>(null);
  const unsubscribeRef = useRef<(() => void) | null>(null);

  useEffect(() => {
    if (!open) {
      unsubscribeRef.current?.();
      unsubscribeRef.current = null;
      setStatus(null);
      setLastEventAt(null);
      return;
    }

    const api = window.tightrope;
    if (!api) return;

    // Seed initial state
    api.getClusterStatus().then((initial) => {
      setStatus(initial);
    }).catch(() => {});

    // Subscribe to fine-grained push events
    const unsub = api.onSyncEvent((event: SyncEvent) => {
      setLastEventAt(event.ts);
      setStatus((prev) => {
        if (!prev) return prev;
        switch (event.type) {
          case 'commit_advance':
            return { ...prev, commit_index: event.commit_index, last_applied: event.last_applied };
          case 'term_change':
            return { ...prev, term: event.term };
          case 'role_change':
            return { ...prev, role: event.role, term: event.term, leader_id: event.leader_id };
          case 'journal_entry':
            return { ...prev, journal_entries: prev.journal_entries + 1 };
          case 'lag_alert':
            return {
              ...prev,
              replication_lag_alert_active: event.active,
              replication_lagging_peers: event.lagging_peers,
              replication_lag_max_entries: event.max_lag,
            };
          case 'peer_state_change': {
            const peers = prev.peers.map((p) =>
              p.site_id === event.site_id ? { ...p, state: event.state } : p
            );
            return { ...prev, peers };
          }
          case 'ingress_batch': {
            const peers = prev.peers.map((p) => {
              if (p.site_id !== event.site_id) return p;
              return event.accepted
                ? {
                    ...p,
                    ingress_accepted_batches: p.ingress_accepted_batches + 1,
                    ingress_accepted_wire_bytes: p.ingress_accepted_wire_bytes + event.bytes,
                    ingress_last_wire_batch_bytes: event.bytes,
                    ingress_last_apply_duration_ms: event.apply_duration_ms,
                    ingress_last_replication_latency_ms: event.replication_latency_ms,
                  }
                : {
                    ...p,
                    ingress_rejected_batches: p.ingress_rejected_batches + 1,
                    ingress_rejected_wire_bytes: p.ingress_rejected_wire_bytes + event.bytes,
                  };
            });
            return { ...prev, peers };
          }
          default:
            return prev;
        }
      });
    });

    unsubscribeRef.current = unsub;
    return () => {
      unsub();
      unsubscribeRef.current = null;
    };
  }, [open]);

  return { status, lastEventAt };
}
```

> Note: `ClusterStatus` does not have a `last_applied` field — it has `pending_raft_entries`. The `commit_advance` case updates `commit_index` only; remove the `last_applied` assignment if it doesn't exist on the interface. Check `shared/types.ts:ClusterStatus` before writing.

- [ ] **Step 4: Run tests — expect PASS**

```
npm --prefix app run test:unit -- --reporter=verbose useSyncTopology
```
Expected: all 5 tests PASS.

---

## Task 11: SyncTopologyDialog component

**Files:**
- Create: `app/src/renderer/components/dialogs/SyncTopologyDialog.tsx`
- Create: `app/src/renderer/components/dialogs/SyncTopologyDialog.test.tsx`

- [ ] **Step 1: Write the failing tests**

Create `app/src/renderer/components/dialogs/SyncTopologyDialog.test.tsx`:

```tsx
import { render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, it, expect, vi } from 'vitest';
import type { ClusterStatus } from '../../shared/types';
import { SyncTopologyDialog } from './SyncTopologyDialog';

const baseStatus: ClusterStatus = {
  enabled: true, site_id: 'site_a3f8', cluster_name: 'prod', role: 'leader', term: 14,
  commit_index: 2891, leader_id: 'site_a3f8', peers: [
    {
      site_id: 'site_b7d2', address: '10.0.1.50:9400', state: 'connected',
      role: 'follower', match_index: 2891, replication_lag_entries: 0,
      consecutive_heartbeat_failures: 0, consecutive_probe_failures: 0,
      ingress_accepted_batches: 10, ingress_rejected_batches: 0,
      ingress_accepted_wire_bytes: 4096, ingress_rejected_wire_bytes: 0,
      ingress_rejected_batch_too_large: 0, ingress_rejected_backpressure: 0,
      ingress_rejected_inflight_wire_budget: 0, ingress_rejected_handshake_auth: 0,
      ingress_rejected_handshake_schema: 0, ingress_rejected_invalid_wire_batch: 0,
      ingress_rejected_entry_limit: 0, ingress_rejected_rate_limit: 0,
      ingress_rejected_apply_batch: 0, ingress_rejected_ingress_protocol: 0,
      ingress_last_wire_batch_bytes: 512, ingress_total_apply_duration_ms: 50,
      ingress_last_apply_duration_ms: 4, ingress_max_apply_duration_ms: 12,
      ingress_apply_duration_ewma_ms: 5.0, ingress_apply_duration_samples: 10,
      ingress_total_replication_latency_ms: 100, ingress_last_replication_latency_ms: 8,
      ingress_max_replication_latency_ms: 20, ingress_replication_latency_ewma_ms: 9.0,
      ingress_replication_latency_samples: 10, ingress_inflight_wire_batches: 0,
      ingress_inflight_wire_batches_peak: 2, ingress_inflight_wire_bytes: 0,
      ingress_inflight_wire_bytes_peak: 1024, last_heartbeat_at: Date.now(),
      last_probe_at: Date.now(), last_probe_duration_ms: 4, last_probe_error: null,
      last_ingress_rejection_at: null, last_ingress_rejection_reason: null,
      last_ingress_rejection_error: null, discovered_via: 'mdns',
    },
  ],
  replication_lagging_peers: 0, replication_lag_total_entries: 0,
  replication_lag_max_entries: 0, replication_lag_avg_entries: 0,
  replication_lag_ewma_entries: 0, replication_lag_ewma_samples: 0,
  replication_lag_alert_threshold_entries: 0, replication_lag_alert_sustained_refreshes: 0,
  replication_lag_alert_streak: 0, replication_lag_alert_active: false,
  replication_lag_last_alert_at: null,
  ingress_socket_accept_failures: 0, ingress_socket_accepted_connections: 1,
  ingress_socket_completed_connections: 1, ingress_socket_failed_connections: 0,
  ingress_socket_active_connections: 1, ingress_socket_peak_active_connections: 1,
  ingress_socket_tls_handshake_failures: 0, ingress_socket_read_failures: 0,
  ingress_socket_apply_failures: 0, ingress_socket_handshake_ack_failures: 0,
  ingress_socket_bytes_read: 4096, ingress_socket_total_connection_duration_ms: 200,
  ingress_socket_last_connection_duration_ms: 200, ingress_socket_max_connection_duration_ms: 200,
  ingress_socket_connection_duration_ewma_ms: 200, ingress_socket_connection_duration_le_10ms: 0,
  ingress_socket_connection_duration_le_50ms: 0, ingress_socket_connection_duration_le_250ms: 1,
  ingress_socket_connection_duration_le_1000ms: 0, ingress_socket_connection_duration_gt_1000ms: 0,
  ingress_socket_max_buffered_bytes: 4096, ingress_socket_max_queued_frames: 1,
  ingress_socket_max_queued_payload_bytes: 512, ingress_socket_paused_read_cycles: 0,
  ingress_socket_paused_read_sleep_ms: 0, ingress_socket_last_connection_at: Date.now(),
  ingress_socket_last_failure_at: null, ingress_socket_last_failure_error: null,
  journal_entries: 1247, pending_raft_entries: 0, last_sync_at: Date.now() - 2000,
};

describe('SyncTopologyDialog', () => {
  it('renders nothing when closed', () => {
    const { container } = render(
      <SyncTopologyDialog open={false} status={null} onClose={() => {}} />
    );
    expect(container.firstChild).toBeNull();
  });

  it('renders the header with leader site_id and term', () => {
    render(<SyncTopologyDialog open status={baseStatus} onClose={() => {}} />);
    expect(screen.getByText('site_a3f8')).toBeInTheDocument();
    expect(screen.getByText('14')).toBeInTheDocument(); // term
    expect(screen.getByText('2,891')).toBeInTheDocument(); // commit_index
  });

  it('renders leader node card', () => {
    render(<SyncTopologyDialog open status={baseStatus} onClose={() => {}} />);
    const cards = screen.getAllByRole('article');
    const leaderCard = cards.find((c) => within(c).queryByText('Leader'));
    expect(leaderCard).toBeTruthy();
  });

  it('renders follower node card for each peer', () => {
    render(<SyncTopologyDialog open status={baseStatus} onClose={() => {}} />);
    expect(screen.getByText('site_b7d2')).toBeInTheDocument();
    expect(screen.getByText('Follower')).toBeInTheDocument();
  });

  it('calls onClose when close button is clicked', async () => {
    const onClose = vi.fn();
    render(<SyncTopologyDialog open status={baseStatus} onClose={onClose} />);
    await userEvent.click(screen.getByLabelText('Close'));
    expect(onClose).toHaveBeenCalledOnce();
  });

  it('shows loading state when status is null', () => {
    render(<SyncTopologyDialog open status={null} onClose={() => {}} />);
    expect(screen.getByText(/loading/i)).toBeInTheDocument();
  });
});
```

- [ ] **Step 2: Run tests — expect FAIL**

```
npm --prefix app run test:unit -- --reporter=verbose SyncTopologyDialog
```
Expected: FAIL with "Cannot find module './SyncTopologyDialog'"

- [ ] **Step 3: Write the component**

Create `app/src/renderer/components/dialogs/SyncTopologyDialog.tsx`:

```tsx
import type { ClusterStatus, ClusterPeerStatus } from '../../shared/types';

interface SyncTopologyDialogProps {
  open: boolean;
  status: ClusterStatus | null;
  onClose: () => void;
}

function formatNumber(n: number): string {
  return n.toLocaleString('en-US');
}

function peerLineColor(peer: ClusterPeerStatus, commitIndex: number): string {
  if (peer.state === 'disconnected' || peer.state === 'unreachable') return 'var(--text-secondary)';
  if (peer.replication_lag_entries > 5) return 'var(--warn)';
  if (peer.replication_lag_entries > 0) return 'var(--accent)';
  return 'var(--ok)';
}

function lagColor(lag: number): string {
  return lag > 0 ? 'var(--warn)' : 'var(--ok)';
}

export function SyncTopologyDialog({ open, status, onClose }: SyncTopologyDialogProps) {
  if (!open) return null;

  return (
    <dialog open onClick={(e) => e.currentTarget === e.target && onClose()}>
      <header className="sync-popup-header">
        <div className="sync-popup-header-left">
          <span className="eyebrow">Cluster</span>
          <h3>Synchronization</h3>
        </div>
        {status && (
          <div className="sync-popup-meta">
            <span>
              {status.role === 'leader' ? 'Leader' : 'Leader'}{' '}
              <strong className="accent-val">{status.site_id}</strong>
            </span>
            <span>Term <strong>{status.term}</strong></span>
            <span>Commit <strong>#{formatNumber(status.commit_index)}</strong></span>
          </div>
        )}
        <button className="dialog-close" type="button" aria-label="Close" onClick={onClose}>
          &times;
        </button>
      </header>

      {!status ? (
        <div className="sync-topology-area" style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', minHeight: '200px' }}>
          <span style={{ color: 'var(--text-secondary)' }}>Loading…</span>
        </div>
      ) : (
        <div className="sync-topology-area">
          <svg className="sync-topology-svg" aria-hidden="true">
            <defs />
            {status.peers.map((peer, i) => {
              // Leader card is centered at top; follower cards in a row at bottom.
              // Use percentage-based positions — SVG lines are decorative only.
              const followerCount = status.peers.length;
              const leaderCx = 50; // percent
              const leaderCy = 20;
              const followerCx = followerCount === 1
                ? 50
                : (i / (followerCount - 1)) * 80 + 10;
              const followerCy = 75;
              return (
                <line
                  key={peer.site_id}
                  x1={`${leaderCx}%`} y1={`${leaderCy}%`}
                  x2={`${followerCx}%`} y2={`${followerCy}%`}
                  stroke={peerLineColor(peer, status.commit_index)}
                  strokeWidth={2}
                  strokeDasharray={peer.replication_lag_entries > 0 ? '6 3' : undefined}
                />
              );
            })}
          </svg>

          <div className="sync-nodes-layer">
            {/* Leader card */}
            <article className="sync-node-card leader" style={{ alignSelf: 'flex-start', margin: '0 auto' }}>
              <div className="sync-node-header">
                <span className="sync-node-site-id">{status.site_id}</span>
                <span className="sync-role-badge leader">Leader</span>
              </div>
              <div className="sync-node-stats">
                <div className="sync-node-stat">
                  <span className="label">Commit</span>
                  <span className="value synced">{formatNumber(status.commit_index)}</span>
                </div>
                <div className="sync-node-stat">
                  <span className="label">Term</span>
                  <span className="value">{status.term}</span>
                </div>
                <div className="sync-node-stat">
                  <span className="label">Journal</span>
                  <span className="value">{formatNumber(status.journal_entries)}</span>
                </div>
              </div>
            </article>

            {/* Follower cards */}
            <div className="sync-follower-row">
              {status.peers.map((peer) => (
                <article key={peer.site_id} className="sync-node-card">
                  <div className="sync-node-header">
                    <span className="sync-node-site-id">{peer.site_id}</span>
                    <span className={`sync-role-badge ${peer.role}`}>
                      {peer.role.charAt(0).toUpperCase() + peer.role.slice(1)}
                    </span>
                  </div>
                  <div className="sync-node-stats">
                    <div className="sync-node-stat">
                      <span className="label">Match</span>
                      <span className="value" style={{ color: lagColor(peer.replication_lag_entries) }}>
                        {formatNumber(peer.match_index)}
                      </span>
                    </div>
                    <div className="sync-node-stat">
                      <span className="label">Lag</span>
                      <span className="value" style={{ color: lagColor(peer.replication_lag_entries) }}>
                        {peer.replication_lag_entries}
                      </span>
                    </div>
                    <div className="sync-node-stat">
                      <span className="label">Probe</span>
                      <span className="value">
                        {peer.last_probe_duration_ms !== null ? `${peer.last_probe_duration_ms}ms` : '—'}
                      </span>
                    </div>
                    <div className="sync-node-stat">
                      <span className="label">State</span>
                      <span
                        className="value"
                        style={{
                          color: peer.state === 'connected'
                            ? 'var(--ok)'
                            : peer.state === 'unreachable'
                            ? 'var(--danger)'
                            : 'var(--warn)',
                        }}
                      >
                        {peer.state}
                      </span>
                    </div>
                  </div>
                </article>
              ))}
            </div>
          </div>
        </div>
      )}

      <div className="sync-popup-footer">
        <div className="sync-legend-item">
          <div className="sync-legend-swatch" style={{ background: 'var(--accent)' }} />
          <span>Replicating down</span>
        </div>
        <div className="sync-legend-item">
          <div className="sync-legend-swatch" style={{ background: '#8a9fd4' }} />
          <span>Replicating up</span>
        </div>
        <div className="sync-legend-item">
          <div className="sync-legend-swatch" style={{ background: 'var(--warn)' }} />
          <span>Lagging</span>
        </div>
        <div className="sync-legend-item">
          <div className="sync-legend-swatch" style={{ background: 'var(--ok)' }} />
          <span>Synced</span>
        </div>
      </div>
    </dialog>
  );
}
```

- [ ] **Step 4: Run tests — expect PASS**

```
npm --prefix app run test:unit -- --reporter=verbose SyncTopologyDialog
```
Expected: all 6 tests PASS.

---

## Task 12: Full integration wiring

**Files:**
- Modify: `app/src/renderer/shared/types.ts` (`AppRuntimeState`)
- Modify: `app/src/renderer/data/seed.ts` (`createInitialRuntimeState`)
- Modify: `app/src/renderer/state/useTightropeState.ts`
- Modify: `app/src/renderer/components/settings/SettingsPage.tsx`
- Modify: `app/src/renderer/components/settings/sections/DatabaseSyncSection.tsx`
- Modify: `app/src/renderer/App.tsx`

### 12a — AppRuntimeState + seed

- [ ] **Step 1: Add `syncTopologyDialogOpen` to `AppRuntimeState`**

In `app/src/renderer/shared/types.ts`, inside `AppRuntimeState` (after `authDialogOpen: boolean`), add:
```ts
syncTopologyDialogOpen: boolean;
```

- [ ] **Step 2: Add initial value in `createInitialRuntimeState`**

In `app/src/renderer/data/seed.ts`, inside `createInitialRuntimeState()` (after `authDialogOpen: false`), add:
```ts
syncTopologyDialogOpen: false,
```

### 12b — useTightropeState

- [ ] **Step 3: Add open/close functions**

In `app/src/renderer/state/useTightropeState.ts`, after `closeAuthDialog` (around line 2243), add:

```ts
function openSyncTopologyDialog(): void {
  setState((previous) => ({ ...previous, syncTopologyDialogOpen: true }));
}

function closeSyncTopologyDialog(): void {
  setState((previous) => ({ ...previous, syncTopologyDialogOpen: false }));
}
```

In the return object at the bottom (around line 2697 where `openBackendDialog` is), add:
```ts
openSyncTopologyDialog,
closeSyncTopologyDialog,
```

### 12c — DatabaseSyncSection

- [ ] **Step 4: Add `onOpenSyncTopology` prop and button**

In `app/src/renderer/components/settings/sections/DatabaseSyncSection.tsx`:

Add to the `DatabaseSyncSectionProps` interface (after `onTriggerSyncNow: () => void`):
```ts
onOpenSyncTopology: () => void;
```

Add to the destructured props:
```ts
onOpenSyncTopology,
```

In the render, find the existing "Trigger sync now" button row (the last `<div className="setting-row">` around line 636). Add the topology button next to it:
```tsx
<div style={{ display: 'flex', justifyContent: 'flex-end', gap: '0.5rem' }}>
  <button className="btn-secondary" type="button" onClick={onTriggerSyncNow}>
    Trigger sync now
  </button>
  <button className="btn-secondary" type="button" onClick={onOpenSyncTopology}>
    View topology
  </button>
</div>
```

### 12d — SettingsPage

- [ ] **Step 5: Thread `onOpenSyncTopology` through SettingsPage**

In `app/src/renderer/components/settings/SettingsPage.tsx`:

Add to `SettingsPageProps` (after `onTriggerSyncNow: () => void`):
```ts
onOpenSyncTopology: () => void;
```

Add to destructured props:
```ts
onOpenSyncTopology,
```

Find the `<DatabaseSyncSection` usage (around line 170) and add the prop:
```tsx
onOpenSyncTopology={onOpenSyncTopology}
```

### 12e — App.tsx

- [ ] **Step 6: Mount the dialog and hook in App.tsx**

In `app/src/renderer/App.tsx`:

Add imports:
```tsx
import { SyncTopologyDialog } from './components/dialogs/SyncTopologyDialog';
import { useSyncTopology } from './state/useSyncTopology';
```

Inside `App()`, after `const model = useTightropeState();`, add:
```tsx
const { status: syncTopologyStatus } = useSyncTopology(model.state.syncTopologyDialogOpen);
```

Add `onOpenSyncTopology` to the `<SettingsPage` props (find the SettingsPage usage, around line 120–170, and add):
```tsx
onOpenSyncTopology={model.openSyncTopologyDialog}
```

After the `<AddAccountDialog ... />` closing tag, add:
```tsx
<SyncTopologyDialog
  open={model.state.syncTopologyDialogOpen}
  status={syncTopologyStatus}
  onClose={model.closeSyncTopologyDialog}
/>
```

- [ ] **Step 7: Run the full test suite**

```
npm --prefix app run test:unit
```
Expected: all tests pass with no TypeScript errors.

- [ ] **Step 8: TypeScript typecheck**

```
npm --prefix app run typecheck
```
Expected: no errors.

---

## Self-Review Notes

- `ClusterStatus` has no `last_applied` field — the `commit_advance` patch in `useSyncTopology.ts` only updates `commit_index`. Remove any `last_applied` reference if not in the interface.
- The `ClusterPeerStatus` type in `shared/types.ts` uses `site_id: string` — the emitter in bridge.cpp must use the string form, not the uint32.
- The `SyncEventEmitter::serialize` is a `static` method called inside a lambda — `SyncEventEmitter::serialize(env, ...)` is valid since it's static.
- The `setup.ts` mock uses `satisfies ElectronApi` — the new `onSyncEvent` stub MUST be added or TypeScript will error on every test run.
- The `SettingsPage` prop chain is the longest threading path — three files (App.tsx → SettingsPage → DatabaseSyncSection) all need the `onOpenSyncTopology` prop added consistently.
