import { useRef, useLayoutEffect } from 'react';
import type { ClusterStatus, ClusterPeerStatus } from '../../shared/types';

interface SyncTopologyDialogProps {
  open: boolean;
  status: ClusterStatus | null;
  onClose: () => void;
}

function fmt(n: number): string {
  return n.toLocaleString();
}

function lineColor(peer: ClusterPeerStatus, commitIndex: number): string {
  if (peer.state !== 'connected') return 'var(--text-secondary)';
  if (peer.replication_lag_entries > 10) return 'var(--warn)';
  if (peer.replication_lag_entries > 0) return 'var(--accent)';
  return 'var(--ok)';
}

function heartbeatDisplay(peer: ClusterPeerStatus): string {
  if (!peer.last_heartbeat_at) return '—';
  const seconds = Math.round((Date.now() - peer.last_heartbeat_at) / 1000);
  return `${seconds}s`;
}

function probeDisplay(peer: ClusterPeerStatus): string {
  if (peer.last_probe_duration_ms === null) return '—';
  return `${peer.last_probe_duration_ms}ms`;
}

interface SvgLine {
  x1: number; y1: number;
  x2: number; y2: number;
  color: string;
}

export function SyncTopologyDialog({ open, status, onClose }: SyncTopologyDialogProps) {
  const leaderRef = useRef<HTMLDivElement>(null);
  const followerRefs = useRef<(HTMLDivElement | null)[]>([]);
  const svgRef = useRef<SVGSVGElement>(null);
  const areaRef = useRef<HTMLDivElement>(null);

  // Draw SVG lines after layout
  useLayoutEffect(() => {
    if (!open || !svgRef.current || !leaderRef.current || !areaRef.current) return;

    const svgEl = svgRef.current;
    const areaRect = areaRef.current.getBoundingClientRect();

    const leaderRect = leaderRef.current.getBoundingClientRect();
    const leaderCx = leaderRect.left - areaRect.left + leaderRect.width / 2;
    const leaderCy = leaderRect.top - areaRect.top + leaderRect.height;

    // Remove old lines
    while (svgEl.firstChild) svgEl.removeChild(svgEl.firstChild);

    followerRefs.current.forEach((ref) => {
      if (!ref) return;
      const rect = ref.getBoundingClientRect();
      const cx = rect.left - areaRect.left + rect.width / 2;
      const cy = rect.top - areaRect.top;

      const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
      line.setAttribute('x1', String(leaderCx));
      line.setAttribute('y1', String(leaderCy));
      line.setAttribute('x2', String(cx));
      line.setAttribute('y2', String(cy));
      line.setAttribute('stroke', 'rgba(255,255,255,0.05)');
      line.setAttribute('stroke-width', '1');
      line.setAttribute('fill', 'none');
      svgEl.appendChild(line);
    });
  });

  if (!open) return null;

  const localId = status?.site_id ?? '—';
  const leaderId = status?.leader_id ?? null;
  const term = status?.term ?? 0;
  const commitIndex = status?.commit_index ?? 0;
  const journalEntries = status?.journal_entries ?? 0;
  const peers = status?.peers ?? [];

  return (
    <dialog
      open
      id="syncTopologyDialog"
      onClick={(e) => e.currentTarget === e.target && onClose()}
    >
      <header className="sync-popup-header">
        <div className="sync-popup-header-left">
          <span className="eyebrow">Cluster</span>
          <h3>Synchronization</h3>
        </div>
        <div className="sync-popup-meta">
          <span>
            Leader{' '}
            <strong className="accent-val">{leaderId ?? localId}</strong>
          </span>
          <span>
            Term <strong>{fmt(term)}</strong>
          </span>
          <span>
            Commit <strong>#{fmt(commitIndex)}</strong>
          </span>
        </div>
        <button className="dialog-close" type="button" aria-label="Close" onClick={onClose}>
          &times;
        </button>
      </header>

      <div className="sync-topology-area" ref={areaRef}>
        <svg className="sync-topology-svg" ref={svgRef} />

        <div className="sync-nodes-layer">
          {/* Leader card */}
          <div className="sync-node-card leader" ref={leaderRef}>
            <div className="sync-node-header">
              <span className="sync-node-site-id">{localId}</span>
              <span className="sync-role-badge leader">Leader</span>
            </div>
            <div className="sync-node-stats">
              <div className="sync-node-stat">
                <span className="label">Commit</span>
                <span className="value synced">{fmt(commitIndex)}</span>
              </div>
              <div className="sync-node-stat">
                <span className="label">Applied</span>
                <span className="value">{fmt(commitIndex)}</span>
              </div>
              <div className="sync-node-stat">
                <span className="label">Term</span>
                <span className="value">{fmt(term)}</span>
              </div>
              <div className="sync-node-stat">
                <span className="label">Log</span>
                <span className="value">{fmt(journalEntries)}</span>
              </div>
            </div>
          </div>

          {/* Follower row */}
          {peers.length > 0 && (
            <div className="sync-follower-row">
              {peers.map((peer, i) => {
                const lagClass = peer.replication_lag_entries > 0 ? 'lag' : 'synced';
                const roleBadge = peer.role === 'leader' ? 'leader'
                               : peer.role === 'candidate' ? 'candidate'
                               : 'follower';
                const roleLabel = peer.role.charAt(0).toUpperCase() + peer.role.slice(1);
                return (
                  <div
                    key={peer.site_id}
                    className="sync-node-card"
                    ref={(el) => { followerRefs.current[i] = el; }}
                  >
                    <div className="sync-node-header">
                      <span className="sync-node-site-id">{peer.site_id}</span>
                      <span className={`sync-role-badge ${roleBadge}`}>{roleLabel}</span>
                    </div>
                    <div className="sync-node-stats">
                      <div className="sync-node-stat">
                        <span className="label">Match</span>
                        <span className={`value ${lagClass}`}>{fmt(peer.match_index)}</span>
                      </div>
                      <div className="sync-node-stat">
                        <span className="label">Lag</span>
                        <span className={`value ${lagClass}`}>{fmt(peer.replication_lag_entries)}</span>
                      </div>
                      <div className="sync-node-stat">
                        <span className="label">Heartbeat</span>
                        <span className="value">{heartbeatDisplay(peer)}</span>
                      </div>
                      <div className="sync-node-stat">
                        <span className="label">Probe</span>
                        <span className="value">{probeDisplay(peer)}</span>
                      </div>
                    </div>
                  </div>
                );
              })}
            </div>
          )}
        </div>
      </div>

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
