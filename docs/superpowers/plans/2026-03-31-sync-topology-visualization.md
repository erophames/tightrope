# Sync Topology Visualization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an animated sync topology popup to `sketch/index.html` that visualizes cluster replication with leader/follower cards, directional arrows showing bidirectional data flow, and payload labels.

**Architecture:** A `<dialog>` element added to the existing sketch HTML. Node cards are HTML elements positioned via flexbox. SVG overlay draws bezier connections between cards. Animated chevrons travel along paths during simulated transfer bursts. All code lives in the single `sketch/index.html` file.

**Tech Stack:** HTML, CSS (custom properties from existing sketch), inline SVG, vanilla JavaScript (requestAnimationFrame for arrow animation)

---

### Task 1: Add CSS for the sync topology popup

**Files:**
- Modify: `sketch/index.html` — insert CSS after line 1362 (before the `/* ── Page switching ── */` comment)

- [ ] **Step 1: Add the sync popup CSS block**

Insert the following CSS immediately before the `/* ── Page switching ── */` comment (line 1363):

```css
      /* ── Sync Topology Popup ── */

      #syncTopologyDialog {
        width: min(580px, 92vw);
        max-height: 85vh;
        overflow: hidden;
      }

      .sync-popup-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 0.6rem 0.75rem;
        border-bottom: 1px solid var(--border);
      }

      .sync-popup-header-left {
        display: grid;
        gap: 0.05rem;
      }

      .sync-popup-header-left .eyebrow {
        font-size: 10px;
        font-weight: 500;
        text-transform: uppercase;
        letter-spacing: 0.05em;
        color: var(--text-tertiary);
      }

      .sync-popup-header-left h3 {
        margin: 0;
        font-size: 13px;
        font-weight: 550;
      }

      .sync-popup-meta {
        display: flex;
        gap: 0.7rem;
        font-size: 10.5px;
        color: var(--text-tertiary);
      }

      .sync-popup-meta strong {
        color: var(--text-secondary);
        font-weight: 500;
        font-family: 'SF Mono', ui-monospace, monospace;
        font-size: 10px;
      }

      .sync-popup-meta .accent-val {
        color: var(--accent);
      }

      .sync-topology-area {
        position: relative;
        padding: 1.2rem 1rem 1rem;
        min-height: 320px;
        overflow: hidden;
      }

      .sync-topology-svg {
        position: absolute;
        inset: 0;
        width: 100%;
        height: 100%;
        pointer-events: none;
        z-index: 1;
      }

      .sync-nodes-layer {
        position: relative;
        z-index: 2;
        display: flex;
        flex-direction: column;
        align-items: center;
        gap: 4.5rem;
      }

      .sync-node-card {
        background: var(--surface-3);
        border: 1px solid var(--border-strong);
        border-radius: var(--radius-md);
        padding: 0.4rem 0.6rem;
        min-width: 140px;
      }

      .sync-node-card.leader {
        border-color: var(--accent-border);
        background: linear-gradient(180deg, rgba(122,171,156,0.04) 0%, var(--surface-3) 100%);
        position: relative;
      }

      .sync-node-card.leader::before {
        content: '';
        position: absolute;
        inset: -1px;
        border-radius: var(--radius-md);
        background: radial-gradient(ellipse 120% 60% at 50% 0%, rgba(122,171,156,0.06), transparent 70%);
        pointer-events: none;
        z-index: -1;
      }

      .sync-node-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        margin-bottom: 0.25rem;
      }

      .sync-node-site-id {
        font-family: 'SF Mono', ui-monospace, monospace;
        font-size: 11px;
        font-weight: 500;
        color: var(--text);
        letter-spacing: -0.02em;
      }

      .sync-role-badge {
        font-size: 9px;
        font-weight: 550;
        text-transform: uppercase;
        letter-spacing: 0.05em;
        padding: 0px 5px;
        border-radius: 3px;
        line-height: 1.6;
      }

      .sync-role-badge.leader {
        color: var(--accent);
        background: var(--accent-dim);
        border: 1px solid var(--accent-border);
      }

      .sync-role-badge.follower {
        color: var(--text-secondary);
        background: rgba(255,255,255,0.03);
        border: 1px solid var(--border);
      }

      .sync-role-badge.candidate {
        color: var(--warn);
        background: rgba(176,143,78,0.08);
        border: 1px solid rgba(176,143,78,0.2);
      }

      .sync-node-stats {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 0.1rem 0.6rem;
      }

      .sync-node-stat {
        display: flex;
        align-items: baseline;
        gap: 0.25rem;
        font-size: 10px;
      }

      .sync-node-stat .label {
        color: var(--text-tertiary);
        font-size: 9.5px;
      }

      .sync-node-stat .value {
        color: var(--text-secondary);
        font-family: 'SF Mono', ui-monospace, monospace;
        font-size: 10px;
        font-weight: 450;
      }

      .sync-node-stat .value.lag { color: var(--warn); }
      .sync-node-stat .value.synced { color: var(--ok); }

      .sync-follower-row {
        display: flex;
        gap: 0.75rem;
        justify-content: center;
        flex-wrap: wrap;
      }

      .sync-connection-line {
        fill: none;
        stroke: rgba(255,255,255,0.05);
        stroke-width: 1;
      }

      .sync-transfer-label {
        position: absolute;
        z-index: 4;
        font-size: 9px;
        font-family: 'SF Mono', ui-monospace, monospace;
        padding: 1px 4px;
        border-radius: 3px;
        white-space: nowrap;
        pointer-events: none;
        transition: opacity 0.25s ease;
      }

      .sync-transfer-label.down {
        color: var(--accent);
        background: rgba(122,171,156,0.08);
        border: 1px solid var(--accent-border);
      }

      .sync-transfer-label.up {
        color: #8a9fd4;
        background: rgba(138,159,212,0.08);
        border: 1px solid rgba(138,159,212,0.2);
      }

      .sync-popup-footer {
        padding: 0.35rem 0.75rem;
        border-top: 1px solid var(--border);
        display: flex;
        gap: 0.8rem;
        font-size: 9.5px;
        color: var(--text-tertiary);
      }

      .sync-legend-item {
        display: flex;
        align-items: center;
        gap: 0.25rem;
      }

      .sync-legend-swatch {
        width: 6px;
        height: 6px;
        border-radius: 2px;
      }
```

- [ ] **Step 2: Verify the file is valid**

Open `sketch/index.html` in a browser, confirm no CSS parse errors (check DevTools console). The page should render exactly as before — no visual changes yet.

- [ ] **Step 3: Commit**

```bash
git add sketch/index.html
git commit -m "style: add CSS for sync topology popup dialog"
```

---

### Task 2: Add the "View topology" trigger button to the cluster status row

**Files:**
- Modify: `sketch/index.html` — replace cluster status row content (lines 3099-3115)

- [ ] **Step 1: Replace the cluster status setting row**

Find this existing HTML (lines 3099-3115):

```html
                  <div class="setting-row" style="border-bottom:none">
                    <div class="setting-label">
                      <strong>Cluster status</strong>
                    </div>
                    <div style="display:grid; gap:0.3rem; font-size:11.5px; text-align:right">
                      <div style="display:flex; gap:0.8rem; align-items:center; justify-content:flex-end">
                        <span style="color:var(--accent); font-weight:500">Leader</span>
                        <span style="color:var(--text-secondary)">Term 14</span>
                        <span style="color:var(--text-secondary)">Commit #2,891</span>
                      </div>
                      <div style="display:flex; gap:0.8rem; align-items:center; justify-content:flex-end">
                        <span style="color:var(--ok)">2 peers connected</span>
                        <span style="color:var(--text-secondary)">Last sync: 2s ago</span>
                        <span style="color:var(--text-secondary)">Journal: 1,247</span>
                      </div>
                    </div>
                  </div>
```

Replace with:

```html
                  <div class="setting-row" style="border-bottom:none">
                    <div class="setting-label">
                      <strong>Cluster status</strong>
                    </div>
                    <div style="display:flex; gap:0.8rem; align-items:center; justify-content:flex-end; font-size:11.5px">
                      <div style="display:grid; gap:0.3rem; text-align:right">
                        <div style="display:flex; gap:0.8rem; align-items:center; justify-content:flex-end">
                          <span style="color:var(--accent); font-weight:500">Leader</span>
                          <span style="color:var(--text-secondary)">Term 14</span>
                          <span style="color:var(--text-secondary)">Commit #2,891</span>
                        </div>
                        <div style="display:flex; gap:0.8rem; align-items:center; justify-content:flex-end">
                          <span style="color:var(--ok)">3 peers connected</span>
                          <span style="color:var(--text-secondary)">Last sync: 2s ago</span>
                          <span style="color:var(--text-secondary)">Journal: 1,247</span>
                        </div>
                      </div>
                      <button class="dock-btn" type="button" id="openSyncTopology">View topology</button>
                    </div>
                  </div>
```

- [ ] **Step 2: Verify the button appears**

Open `sketch/index.html`, navigate to Settings → Database synchronization. The "Cluster status" row should now show a "View topology" button on the right side next to the stats.

- [ ] **Step 3: Commit**

```bash
git add sketch/index.html
git commit -m "feat: add View topology button to cluster status row"
```

---

### Task 3: Add the sync topology dialog HTML

**Files:**
- Modify: `sketch/index.html` — insert dialog after the OAuth dialog (after line 3281, which is `</dialog>` closing the authDialog)

- [ ] **Step 1: Add the dialog HTML**

Insert the following immediately after the `</dialog>` that closes `authDialog` (after line 3281):

```html

    <!-- Sync Topology dialog -->
    <dialog id="syncTopologyDialog">
      <header class="sync-popup-header">
        <div class="sync-popup-header-left">
          <span class="eyebrow">Cluster</span>
          <h3>Synchronization</h3>
        </div>
        <div class="sync-popup-meta">
          <span>Leader <strong class="accent-val">site_a3f8</strong></span>
          <span>Term <strong>14</strong></span>
          <span>Commit <strong>#2,891</strong></span>
        </div>
        <button class="dialog-close" id="closeSyncTopology" type="button" aria-label="Close">&times;</button>
      </header>

      <div class="sync-topology-area" id="syncTopologyArea">
        <svg class="sync-topology-svg" id="syncSvgLayer"><defs></defs></svg>

        <div class="sync-nodes-layer" id="syncNodesLayer">
          <!-- Leader -->
          <div class="sync-node-card leader" id="syncNodeLeader">
            <div class="sync-node-header">
              <span class="sync-node-site-id">site_a3f8</span>
              <span class="sync-role-badge leader">Leader</span>
            </div>
            <div class="sync-node-stats">
              <div class="sync-node-stat">
                <span class="label">Commit</span>
                <span class="value synced">2,891</span>
              </div>
              <div class="sync-node-stat">
                <span class="label">Applied</span>
                <span class="value">2,891</span>
              </div>
              <div class="sync-node-stat">
                <span class="label">Term</span>
                <span class="value">14</span>
              </div>
              <div class="sync-node-stat">
                <span class="label">Log</span>
                <span class="value">2,894</span>
              </div>
            </div>
          </div>

          <!-- Followers -->
          <div class="sync-follower-row">
            <div class="sync-node-card" id="syncNodeF1">
              <div class="sync-node-header">
                <span class="sync-node-site-id">site_b7d2</span>
                <span class="sync-role-badge follower">Follower</span>
              </div>
              <div class="sync-node-stats">
                <div class="sync-node-stat">
                  <span class="label">Match</span>
                  <span class="value synced">2,891</span>
                </div>
                <div class="sync-node-stat">
                  <span class="label">Lag</span>
                  <span class="value synced">0</span>
                </div>
                <div class="sync-node-stat">
                  <span class="label">Heartbeat</span>
                  <span class="value">1s</span>
                </div>
                <div class="sync-node-stat">
                  <span class="label">Probe</span>
                  <span class="value">4ms</span>
                </div>
              </div>
            </div>

            <div class="sync-node-card" id="syncNodeF2">
              <div class="sync-node-header">
                <span class="sync-node-site-id">site_c9e4</span>
                <span class="sync-role-badge follower">Follower</span>
              </div>
              <div class="sync-node-stats">
                <div class="sync-node-stat">
                  <span class="label">Match</span>
                  <span class="value lag">2,884</span>
                </div>
                <div class="sync-node-stat">
                  <span class="label">Lag</span>
                  <span class="value lag">7</span>
                </div>
                <div class="sync-node-stat">
                  <span class="label">Heartbeat</span>
                  <span class="value">3s</span>
                </div>
                <div class="sync-node-stat">
                  <span class="label">Probe</span>
                  <span class="value">12ms</span>
                </div>
              </div>
            </div>

            <div class="sync-node-card" id="syncNodeF3">
              <div class="sync-node-header">
                <span class="sync-node-site-id">site_d1a7</span>
                <span class="sync-role-badge follower">Follower</span>
              </div>
              <div class="sync-node-stats">
                <div class="sync-node-stat">
                  <span class="label">Match</span>
                  <span class="value synced">2,890</span>
                </div>
                <div class="sync-node-stat">
                  <span class="label">Lag</span>
                  <span class="value synced">1</span>
                </div>
                <div class="sync-node-stat">
                  <span class="label">Heartbeat</span>
                  <span class="value">1s</span>
                </div>
                <div class="sync-node-stat">
                  <span class="label">Probe</span>
                  <span class="value">6ms</span>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>

      <div class="sync-popup-footer">
        <div class="sync-legend-item">
          <div class="sync-legend-swatch" style="background:var(--accent)"></div>
          <span>Replicating down</span>
        </div>
        <div class="sync-legend-item">
          <div class="sync-legend-swatch" style="background:#8a9fd4"></div>
          <span>Replicating up</span>
        </div>
        <div class="sync-legend-item">
          <div class="sync-legend-swatch" style="background:var(--warn)"></div>
          <span>Lagging</span>
        </div>
        <div class="sync-legend-item">
          <div class="sync-legend-swatch" style="background:var(--ok)"></div>
          <span>Synced</span>
        </div>
      </div>
    </dialog>
```

- [ ] **Step 2: Verify the dialog element exists**

Open `sketch/index.html`, open DevTools, run: `document.getElementById('syncTopologyDialog')`. Should return the dialog element (not null).

- [ ] **Step 3: Commit**

```bash
git add sketch/index.html
git commit -m "feat: add sync topology dialog HTML with node cards"
```

---

### Task 4: Wire up open/close behavior for the dialog

**Files:**
- Modify: `sketch/index.html` — add JavaScript after the existing dialog element references (after line 3540)

- [ ] **Step 1: Add dialog element reference and open/close wiring**

Find this line (around line 3540):

```javascript
      const addAccountDialogEl = document.getElementById("addAccountDialog");
```

Insert immediately after it:

```javascript
      const syncTopologyDialogEl = document.getElementById("syncTopologyDialog");
```

- [ ] **Step 2: Add the sync topology dialog to the backdrop-click-to-close array**

Find this line (around line 4410):

```javascript
      [backendDialogEl, authDialogEl, addAccountDialogEl].forEach((dialog) => {
```

Replace with:

```javascript
      [backendDialogEl, authDialogEl, addAccountDialogEl, syncTopologyDialogEl].forEach((dialog) => {
```

- [ ] **Step 3: Add open/close event listeners**

Find this line (around line 4448, after the `openAddAccount2` listener):

```javascript
      document.getElementById("openAddAccount2").addEventListener("click", () => {
        resetAddAccountDialog();
        addAccountDialogEl.showModal();
      });
```

Insert immediately after it:

```javascript

      // ── Sync Topology dialog ──
      document.getElementById("openSyncTopology").addEventListener("click", () => {
        syncTopologyDialogEl.showModal();
        syncDrawConnections();
        syncStartTraffic();
      });
      document.getElementById("closeSyncTopology").addEventListener("click", () => {
        syncTopologyDialogEl.close();
        syncStopTraffic();
      });
```

- [ ] **Step 4: Verify open/close works**

Open `sketch/index.html`, go to Settings → Database synchronization, click "View topology". The dialog should open (empty topology area for now — connections and arrows come next). Click the X or backdrop to close.

- [ ] **Step 5: Commit**

```bash
git add sketch/index.html
git commit -m "feat: wire sync topology dialog open/close behavior"
```

---

### Task 5: Add SVG connection drawing logic

**Files:**
- Modify: `sketch/index.html` — add JavaScript before the closing `</script>` tag (before line 4976)

- [ ] **Step 1: Add the connection drawing code**

Insert the following before the `renderStrategyDetail();` call near the end of the script (before line 4975):

```javascript

      // ── Sync Topology: SVG connections ──

      const syncSvgLayer = document.getElementById("syncSvgLayer");
      const syncTopologyArea = document.getElementById("syncTopologyArea");
      const syncLeaderEl = document.getElementById("syncNodeLeader");
      const syncFollowerIds = ["syncNodeF1", "syncNodeF2", "syncNodeF3"];

      function syncGetEdge(el, side) {
        const r = el.getBoundingClientRect();
        const cr = syncTopologyArea.getBoundingClientRect();
        const cx = r.left - cr.left + r.width / 2;
        if (side === "bottom") return { x: cx, y: r.top - cr.top + r.height };
        if (side === "top") return { x: cx, y: r.top - cr.top };
      }

      function syncDrawConnections() {
        // Remove old paths (keep defs)
        syncSvgLayer.querySelectorAll("path").forEach((p) => p.remove());

        const lBottom = syncGetEdge(syncLeaderEl, "bottom");
        syncFollowerIds.forEach((id, i) => {
          const fEl = document.getElementById(id);
          const fTop = syncGetEdge(fEl, "top");
          const midY = (lBottom.y + fTop.y) / 2;
          const d = `M${lBottom.x},${lBottom.y} C${lBottom.x},${midY} ${fTop.x},${midY} ${fTop.x},${fTop.y}`;

          const path = document.createElementNS("http://www.w3.org/2000/svg", "path");
          path.setAttribute("d", d);
          path.setAttribute("class", "sync-connection-line");
          path.setAttribute("id", `syncConn${i}`);
          syncSvgLayer.appendChild(path);
        });
      }
```

- [ ] **Step 2: Verify connections draw**

Open the dialog. Three bezier curves should connect the leader card to each follower card. Resize the browser — the lines should stay connected (they redraw on open).

- [ ] **Step 3: Commit**

```bash
git add sketch/index.html
git commit -m "feat: draw SVG bezier connections between leader and followers"
```

---

### Task 6: Add animated directional arrow chevrons and transfer labels

**Files:**
- Modify: `sketch/index.html` — add JavaScript immediately after the `syncDrawConnections` function (continuing the same script section)

- [ ] **Step 1: Add the chevron animation and transfer label functions**

Insert immediately after the `syncDrawConnections` function closing brace:

```javascript

      // ── Sync Topology: Animated arrows ──

      const SYNC_ACCENT = [122, 171, 156];  // down-replication color (green)
      const SYNC_UP_COLOR = [138, 159, 212]; // up-replication color (blue)

      function syncAnimateChevrons(pathId, direction, count, rgb) {
        const path = document.getElementById(pathId);
        if (!path) return;
        const len = path.getTotalLength();
        const stagger = 100;

        for (let c = 0; c < count; c++) {
          setTimeout(() => {
            const tri = document.createElementNS("http://www.w3.org/2000/svg", "polygon");
            const dur = 1000;
            const start = performance.now();
            const forward = direction === "down";

            function step(now) {
              const t = Math.min((now - start) / dur, 1);
              const dist = forward ? t * len : (1 - t) * len;
              const pt = path.getPointAtLength(dist);

              const d2 = Math.min(dist + 2, len);
              const d1 = Math.max(dist - 2, 0);
              const p1 = path.getPointAtLength(d1);
              const p2 = path.getPointAtLength(d2);
              const angle = Math.atan2(p2.y - p1.y, p2.x - p1.x) + (forward ? 0 : Math.PI);

              const sz = 3.5;
              const tip = { x: pt.x + Math.cos(angle) * sz, y: pt.y + Math.sin(angle) * sz };
              const l = { x: pt.x + Math.cos(angle + 2.4) * sz * 0.85, y: pt.y + Math.sin(angle + 2.4) * sz * 0.85 };
              const r = { x: pt.x + Math.cos(angle - 2.4) * sz * 0.85, y: pt.y + Math.sin(angle - 2.4) * sz * 0.85 };
              tri.setAttribute("points", `${tip.x},${tip.y} ${l.x},${l.y} ${r.x},${r.y}`);

              let opacity = 1;
              if (t < 0.1) opacity = t / 0.1;
              if (t > 0.85) opacity = (1 - t) / 0.15;
              tri.setAttribute("fill", `rgba(${rgb[0]},${rgb[1]},${rgb[2]},${opacity})`);

              if (t < 1) requestAnimationFrame(step);
              else tri.remove();
            }

            syncSvgLayer.appendChild(tri);
            requestAnimationFrame(step);
          }, c * stagger);
        }
      }

      function syncShowTransferLabel(pathId, text, direction) {
        const path = document.getElementById(pathId);
        if (!path) return;
        const len = path.getTotalLength();
        const pt = path.getPointAtLength(len * 0.5);

        const label = document.createElement("div");
        label.className = "sync-transfer-label " + direction;
        label.textContent = text;
        label.style.left = (pt.x + 8) + "px";
        label.style.top = (pt.y - 7) + "px";
        label.style.opacity = "0";
        syncTopologyArea.appendChild(label);

        requestAnimationFrame(() => { label.style.opacity = "1"; });
        setTimeout(() => {
          label.style.opacity = "0";
          setTimeout(() => label.remove(), 300);
        }, 1800);
      }

      // ── Sync Topology: Simulated traffic bursts ──

      let syncTrafficInterval = null;

      function syncRunCycle() {
        // Leader → Follower 2 (catching up — lagging by 7)
        syncAnimateChevrons("syncConn1", "down", 4, SYNC_ACCENT);
        syncShowTransferLabel("syncConn1", "7 entries · 2.1 KB", "down");

        // Follower 1 → Leader (CRDT data pushed up)
        setTimeout(() => {
          syncAnimateChevrons("syncConn0", "up", 3, SYNC_UP_COLOR);
          syncShowTransferLabel("syncConn0", "2 entries · 640 B", "up");
        }, 1400);

        // Leader → Follower 3 (small catch-up)
        setTimeout(() => {
          syncAnimateChevrons("syncConn2", "down", 2, SYNC_ACCENT);
          syncShowTransferLabel("syncConn2", "1 entry · 340 B", "down");
        }, 2800);

        // Follower 3 → Leader (usage counters)
        setTimeout(() => {
          syncAnimateChevrons("syncConn2", "up", 2, SYNC_UP_COLOR);
          syncShowTransferLabel("syncConn2", "3 entries · 512 B", "up");
        }, 4200);
      }

      function syncStartTraffic() {
        syncStopTraffic();
        // Small delay to let dialog layout settle before first draw
        setTimeout(() => {
          syncRunCycle();
          syncTrafficInterval = setInterval(syncRunCycle, 6000);
        }, 100);
      }

      function syncStopTraffic() {
        if (syncTrafficInterval) {
          clearInterval(syncTrafficInterval);
          syncTrafficInterval = null;
        }
        // Clean up any lingering transfer labels
        syncTopologyArea.querySelectorAll(".sync-transfer-label").forEach((el) => el.remove());
      }
```

- [ ] **Step 2: Verify animated arrows work**

Open the dialog. After ~100ms you should see:
1. Green chevrons flowing from leader down to the second follower (site_c9e4) with a label "7 entries · 2.1 KB"
2. After 1.4s, blue chevrons flowing from the first follower (site_b7d2) up to the leader with "2 entries · 640 B"
3. After 2.8s, green chevrons down to third follower with "1 entry · 340 B"
4. After 4.2s, blue chevrons up from third follower with "3 entries · 512 B"
5. Cycle repeats every 6 seconds

Close and reopen — animation should restart cleanly with no lingering labels.

- [ ] **Step 3: Commit**

```bash
git add sketch/index.html
git commit -m "feat: add animated directional arrows and transfer labels to sync topology"
```

---

### Task 7: Final integration verification

**Files:**
- No file changes — verification only

- [ ] **Step 1: Full walkthrough**

1. Open `sketch/index.html` in a browser
2. Navigate to Settings page
3. Scroll to "Database synchronization" section
4. Verify "View topology" button is visible on the "Cluster status" row
5. Click it — dialog opens centered with backdrop
6. Verify: leader card top-center with accent styling, 3 follower cards below in a row
7. Verify: bezier SVG lines connect leader to each follower
8. Verify: animated arrow bursts appear — green down, blue up — with payload labels
9. Verify: labels fade in and out smoothly
10. Close via X button — animation stops, dialog closes
11. Close via backdrop click — same behavior
12. Reopen — animation restarts fresh

- [ ] **Step 2: Test with light theme**

Switch the theme selector to "Light". Reopen the topology dialog. Verify cards and text are readable in light mode (the existing CSS variables should handle this automatically since we use `--surface-3`, `--text`, `--border`, etc.).

- [ ] **Step 3: Test browser resize**

With the dialog open, resize the browser window. The follower cards should wrap if the dialog gets narrow. Close and reopen — connections should redraw correctly for the new layout.
