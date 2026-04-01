# Sync Topology Visualization

**Date:** 2026-03-31
**Status:** Draft

## Summary

An animated popup dialog accessible from the settings page "Database synchronization" section that visualizes real-time cluster replication topology. Shows which node is leader, per-peer replication progress, and animated directional arrows representing active data transfers with payload sizes.

## Trigger

A clickable element on the "Cluster status" row of the Database Synchronization settings group in `sketch/index.html`. Clicking opens a centered popup overlay.

## Layout

**Popup dialog** — approximately 560px wide, 480px tall, centered over the app with a backdrop overlay. Matches the existing dialog styling in the sketch (rounded corners, surface-1 background, border, box shadow).

**Structure:**
- **Header bar** — title "Synchronization" with eyebrow "Cluster", cluster metadata chips (leader site ID, current term, commit index), close button
- **Topology area** — the main visualization canvas
- **Footer** — compact legend for arrow colors and status indicators

## Node Topology

**Spatial hierarchy:**
- Leader node card positioned top-center
- Follower/candidate node cards in a horizontal row below
- SVG layer behind/over the cards draws bezier curve connections between leader and each follower

**Scaling to N nodes:**
- Follower row uses flexbox with wrapping — as nodes are added they flow into additional rows
- SVG connections are recalculated dynamically on layout changes
- No hardcoded positions; all coordinates derived from DOM element positions

**Node cards** are compact rectangular cards (not circles) containing:
- Site ID in monospace font
- Role badge: Leader (accent green), Follower (muted), Candidate (warn yellow)
- Stats grid (2 columns):
  - Leader: Commit index, Applied index, Term, Log index
  - Follower: Match index, Replication lag, Last heartbeat age, Probe latency
- Leader card has subtle accent border and top gradient glow
- Lagging values shown in warn color, synced values in ok green

## Animated Data Flow Arrows

**Event-driven bursts** — arrows appear only when data is actively transferring, then disappear when idle. No ambient animation.

**Implementation:**
- Small filled SVG triangles (chevrons) animated along the bezier path connecting two nodes using `requestAnimationFrame`
- Each burst sends 2-4 staggered chevrons traveling from sender to receiver
- Chevrons fade in near the start of the path and fade out near the end

**Bidirectional traffic:**
- **Down-replication** (leader to follower): accent green (`#7aab9c`) chevrons flowing downward — Raft log entries, consensus data
- **Up-replication** (follower to leader): soft blue (`#8a9fd4`) chevrons flowing upward — CRDT journal entries, usage counters, local writes

**Payload labels:**
- Brief label appears at the midpoint of the connection during a transfer burst
- Shows entry count and byte size, e.g. "7 entries · 2.1 KB"
- Color-coded to match direction (green border for down, blue border for up)
- Fades in, holds for ~1.8s, fades out

## Data Source

The visualization is driven by `ClusterStatus` from the Bridge API:

```
ClusterStatus {
  enabled, site_id, cluster_name, role, term, commit_index,
  leader_id, journal_entries, pending_raft_entries, last_sync_at,
  peers: [PeerStatus {
    site_id, address, state, role, match_index,
    replication_lag_entries, consecutive_heartbeat_failures,
    consecutive_probe_failures, last_heartbeat_at, last_probe_at,
    last_probe_duration_ms, last_probe_error, discovered_via
  }]
}
```

For the sketch, this data is simulated with hardcoded values and a JavaScript timer that triggers synthetic transfer bursts on a loop. In the real app, the visualization would poll `cluster_status()` and diff `match_index` / `commit_index` changes to determine when transfers are happening and in which direction.

## Styling

Follows the existing sketch design language exactly:
- CSS custom properties: `--bg`, `--surface-1/2/3`, `--border`, `--accent`, `--ok`, `--warn`, `--text`, `--text-secondary`, `--text-tertiary`
- Typography: Inter for UI text, JetBrains Mono for site IDs, indices, and payload labels
- Border radius: `--radius-md` for cards, 10px for the popup
- Compact font sizes: 9-11px for stats and labels, 13px for header

## Integration into Sketch

The popup is added directly to `sketch/index.html`:

1. **CSS** — new styles added in the existing `<style>` block for `.sync-popup`, `.sync-backdrop`, node cards, SVG connections, and arrow animations
2. **HTML** — a new `<dialog>` element (or div with backdrop) appended after the existing dialogs, plus a clickable "View topology" button/link added to the "Cluster status" setting row in the Database Synchronization section
3. **JavaScript** — connection drawing, arrow animation, and simulated traffic burst logic added to the existing `<script>` block. Wired to the open/close button.

No new files. Everything lives in the single `sketch/index.html`.

## Interaction

- Click "View topology" link/button on the cluster status row to open
- Click close button or backdrop to dismiss
- No other interactive elements; this is a read-only visualization

## What This Is Not

- Not a configuration interface — no editing cluster settings from this popup
- Not a log viewer — no scrollable list of journal entries
- Not a network diagram — no inter-follower connections shown (all connections are leader-to-peer)
