import type { ClusterStatus, SyncConflictResolution } from '../../../shared/types';

interface DatabaseSyncSectionProps {
  syncEnabled: boolean;
  syncSiteId: number;
  syncPort: number;
  syncDiscoveryEnabled: boolean;
  syncClusterName: string;
  manualPeerAddress: string;
  syncIntervalSeconds: number;
  syncConflictResolution: SyncConflictResolution;
  syncJournalRetentionDays: number;
  syncTlsEnabled: boolean;
  syncRequireHandshakeAuth: boolean;
  syncClusterSharedSecret: string;
  syncTlsVerifyPeer: boolean;
  syncTlsCaCertificatePath: string;
  syncTlsCertificateChainPath: string;
  syncTlsPrivateKeyPath: string;
  syncTlsPinnedPeerCertificateSha256: string;
  syncSchemaVersion: number;
  syncMinSupportedSchemaVersion: number;
  syncAllowSchemaDowngrade: boolean;
  syncPeerProbeEnabled: boolean;
  syncPeerProbeIntervalMs: number;
  syncPeerProbeTimeoutMs: number;
  syncPeerProbeMaxPerRefresh: number;
  syncPeerProbeFailClosed: boolean;
  syncPeerProbeFailClosedFailures: number;
  clusterStatus: ClusterStatus;
  onToggleSyncEnabled: () => void;
  onSetSyncSiteId: (siteId: number) => void;
  onSetSyncPort: (port: number) => void;
  onSetSyncDiscoveryEnabled: (enabled: boolean) => void;
  onSetSyncClusterName: (clusterName: string) => void;
  onSetManualPeerAddress: (value: string) => void;
  onAddManualPeer: () => void;
  onRemovePeer: (siteId: string) => void;
  onSetSyncIntervalSeconds: (seconds: number) => void;
  onSetSyncConflictResolution: (strategy: SyncConflictResolution) => void;
  onSetSyncJournalRetentionDays: (days: number) => void;
  onSetSyncTlsEnabled: (enabled: boolean) => void;
  onSetSyncRequireHandshakeAuth: (enabled: boolean) => void;
  onSetSyncClusterSharedSecret: (secret: string) => void;
  onSetSyncTlsVerifyPeer: (enabled: boolean) => void;
  onSetSyncTlsCaCertificatePath: (path: string) => void;
  onSetSyncTlsCertificateChainPath: (path: string) => void;
  onSetSyncTlsPrivateKeyPath: (path: string) => void;
  onSetSyncTlsPinnedPeerCertificateSha256: (value: string) => void;
  onSetSyncSchemaVersion: (version: number) => void;
  onSetSyncMinSupportedSchemaVersion: (version: number) => void;
  onSetSyncAllowSchemaDowngrade: (enabled: boolean) => void;
  onSetSyncPeerProbeEnabled: (enabled: boolean) => void;
  onSetSyncPeerProbeIntervalMs: (value: number) => void;
  onSetSyncPeerProbeTimeoutMs: (value: number) => void;
  onSetSyncPeerProbeMaxPerRefresh: (value: number) => void;
  onSetSyncPeerProbeFailClosed: (enabled: boolean) => void;
  onSetSyncPeerProbeFailClosedFailures: (value: number) => void;
  onTriggerSyncNow: () => void;
  onOpenSyncTopology: () => void;
}

function formatLastSync(lastSyncAt: number | null): string {
  if (lastSyncAt === null) return 'never';
  const deltaSeconds = Math.max(0, Math.floor((Date.now() - lastSyncAt) / 1000));
  if (deltaSeconds < 60) return `${deltaSeconds}s ago`;
  const minutes = Math.floor(deltaSeconds / 60);
  if (minutes < 60) return `${minutes}m ago`;
  const hours = Math.floor(minutes / 60);
  return `${hours}h ago`;
}

export function DatabaseSyncSection({
  syncEnabled,
  syncSiteId,
  syncPort,
  syncDiscoveryEnabled,
  syncClusterName,
  manualPeerAddress,
  syncIntervalSeconds,
  syncConflictResolution,
  syncJournalRetentionDays,
  syncTlsEnabled,
  syncRequireHandshakeAuth,
  syncClusterSharedSecret,
  syncTlsVerifyPeer,
  syncTlsCaCertificatePath,
  syncTlsCertificateChainPath,
  syncTlsPrivateKeyPath,
  syncTlsPinnedPeerCertificateSha256,
  syncSchemaVersion,
  syncMinSupportedSchemaVersion,
  syncAllowSchemaDowngrade,
  syncPeerProbeEnabled,
  syncPeerProbeIntervalMs,
  syncPeerProbeTimeoutMs,
  syncPeerProbeMaxPerRefresh,
  syncPeerProbeFailClosed,
  syncPeerProbeFailClosedFailures,
  clusterStatus,
  onToggleSyncEnabled,
  onSetSyncSiteId,
  onSetSyncPort,
  onSetSyncDiscoveryEnabled,
  onSetSyncClusterName,
  onSetManualPeerAddress,
  onAddManualPeer,
  onRemovePeer,
  onSetSyncIntervalSeconds,
  onSetSyncConflictResolution,
  onSetSyncJournalRetentionDays,
  onSetSyncTlsEnabled,
  onSetSyncRequireHandshakeAuth,
  onSetSyncClusterSharedSecret,
  onSetSyncTlsVerifyPeer,
  onSetSyncTlsCaCertificatePath,
  onSetSyncTlsCertificateChainPath,
  onSetSyncTlsPrivateKeyPath,
  onSetSyncTlsPinnedPeerCertificateSha256,
  onSetSyncSchemaVersion,
  onSetSyncMinSupportedSchemaVersion,
  onSetSyncAllowSchemaDowngrade,
  onSetSyncPeerProbeEnabled,
  onSetSyncPeerProbeIntervalMs,
  onSetSyncPeerProbeTimeoutMs,
  onSetSyncPeerProbeMaxPerRefresh,
  onSetSyncPeerProbeFailClosed,
  onSetSyncPeerProbeFailClosedFailures,
  onTriggerSyncNow,
  onOpenSyncTopology,
}: DatabaseSyncSectionProps) {
  const peers = clusterStatus.peers ?? [];
  const connectedPeers = peers.filter((peer) => peer.state === 'connected').length;
  const unreachablePeers = peers.filter((peer) => peer.state === 'unreachable').length;

  return (
    <div className="settings-group">
      <div className="settings-group-header">
        <h3>Database synchronization</h3>
        <p>Bidirectional sync between instances using journaled change replication with conflict resolution.</p>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Enable sync</strong>
          <span>Activate bidirectional replication with a remote instance</span>
        </div>
        <button
          className={`setting-toggle${syncEnabled ? ' on' : ''}`}
          type="button"
          aria-label="Toggle sync"
          onClick={onToggleSyncEnabled}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Instance ID</strong>
          <span>Unique site identifier for this node</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={1}
          value={syncSiteId}
          onChange={(event) => onSetSyncSiteId(Math.max(1, Number(event.target.value) || 1))}
          style={{ width: '90px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Sync port</strong>
          <span>TCP port for peer replication traffic</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={1}
          max={65535}
          value={syncPort}
          onChange={(event) => onSetSyncPort(Math.min(65535, Math.max(1, Number(event.target.value) || 1)))}
          style={{ width: '90px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Peer discovery</strong>
          <span>Find instances on the local network via mDNS (Bonjour/Avahi)</span>
        </div>
        <button
          className={`setting-toggle${syncDiscoveryEnabled ? ' on' : ''}`}
          type="button"
          aria-label="Toggle peer discovery"
          onClick={() => onSetSyncDiscoveryEnabled(!syncDiscoveryEnabled)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Cluster name</strong>
          <span>Only sync with peers advertising the same cluster</span>
        </div>
        <input
          className="setting-input"
          type="text"
          value={syncClusterName}
          onChange={(event) => onSetSyncClusterName(event.target.value)}
          style={{ width: '150px', textAlign: 'right' }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Discovered peers</strong>
          <span>Instances currently visible to this node</span>
        </div>
        <div style={{ display: 'grid', gap: '0.35rem', justifyItems: 'end' }}>
          {peers.length === 0 ? (
            <span style={{ color: 'var(--text-secondary)', fontSize: '11.5px' }}>none</span>
          ) : (
            peers.map((peer) => (
              <div key={`${peer.site_id}-${peer.address}`} style={{ display: 'flex', gap: '0.35rem', alignItems: 'center' }}>
                <span style={{ fontFamily: "'SF Mono',ui-monospace,monospace", color: 'var(--text-secondary)' }}>{peer.site_id}</span>
                <span
                  style={{
                    color: peer.state === 'connected' ? 'var(--ok)' : peer.state === 'unreachable' ? 'var(--danger)' : 'var(--warn)',
                  }}
                >
                  {peer.address}
                </span>
                <span style={{ color: 'var(--text-secondary)', fontSize: '11px' }}>
                  {peer.state} · lag {peer.replication_lag_entries} · hb failures {peer.consecutive_heartbeat_failures} · probe failures{' '}
                  {peer.consecutive_probe_failures} · source {peer.discovered_via} · last probe {formatLastSync(peer.last_probe_at)}
                  {peer.last_probe_duration_ms !== null ? ` · probe ${peer.last_probe_duration_ms}ms` : ''}
                  {peer.last_probe_error ? ` · probe error: ${peer.last_probe_error}` : ''}
                </span>
                <button className="btn-danger" type="button" style={{ fontSize: '11px', padding: '0.15rem 0.4rem' }} onClick={() => onRemovePeer(peer.site_id)}>
                  Remove
                </button>
              </div>
            ))
          )}
        </div>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Manual peer</strong>
          <span>Fallback address for cross-subnet peers</span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.35rem' }}>
          <input
            className="setting-input"
            type="text"
            placeholder="host:port"
            value={manualPeerAddress}
            onChange={(event) => onSetManualPeerAddress(event.target.value)}
            style={{ width: '160px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
          />
          <button className="btn-secondary" type="button" onClick={onAddManualPeer}>
            Add peer
          </button>
        </div>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Sync interval</strong>
          <span>Seconds between sync cycles (0 = manual trigger)</span>
        </div>
        <input
          className="setting-input"
          type="number"
          value={syncIntervalSeconds}
          min={0}
          max={86400}
          onChange={(event) => onSetSyncIntervalSeconds(Math.max(0, Number(event.target.value) || 0))}
          style={{ width: '70px', textAlign: 'right' }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Conflict resolution</strong>
          <span>Strategy when both instances modify the same row</span>
        </div>
        <select
          className="setting-select"
          style={{ minWidth: '180px' }}
          value={syncConflictResolution}
          onChange={(event) => onSetSyncConflictResolution(event.target.value as SyncConflictResolution)}
        >
          <option value="lww">Last-writer-wins (HLC)</option>
          <option value="site_priority">Site priority</option>
          <option value="field_merge">Per-field merge</option>
        </select>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Journal retention</strong>
          <span>Days to keep change journal entries before compaction</span>
        </div>
        <input
          className="setting-input"
          type="number"
          value={syncJournalRetentionDays}
          min={1}
          max={3650}
          onChange={(event) => onSetSyncJournalRetentionDays(Math.max(1, Number(event.target.value) || 1))}
          style={{ width: '70px', textAlign: 'right' }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Transport encryption</strong>
          <span>TLS for sync traffic between instances</span>
        </div>
        <button
          className={`setting-toggle${syncTlsEnabled ? ' on' : ''}`}
          type="button"
          aria-label="Toggle sync tls"
          onClick={() => onSetSyncTlsEnabled(!syncTlsEnabled)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Handshake auth</strong>
          <span>Require signed peer handshakes with a shared cluster secret</span>
        </div>
        <button
          className={`setting-toggle${syncRequireHandshakeAuth ? ' on' : ''}`}
          type="button"
          aria-label="Toggle handshake auth"
          onClick={() => onSetSyncRequireHandshakeAuth(!syncRequireHandshakeAuth)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Cluster shared secret</strong>
          <span>Pre-shared secret used for handshake HMAC authentication</span>
        </div>
        <input
          className="setting-input"
          type="password"
          placeholder="cluster secret"
          value={syncClusterSharedSecret}
          onChange={(event) => onSetSyncClusterSharedSecret(event.target.value)}
          style={{ width: '220px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>TLS peer verification</strong>
          <span>Validate peer certificate chain for sync connections</span>
        </div>
        <button
          className={`setting-toggle${syncTlsVerifyPeer ? ' on' : ''}`}
          type="button"
          aria-label="Toggle sync tls peer verification"
          onClick={() => onSetSyncTlsVerifyPeer(!syncTlsVerifyPeer)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>TLS CA bundle path</strong>
          <span>PEM file used to verify peer certificate chain</span>
        </div>
        <input
          className="setting-input"
          type="text"
          placeholder="/path/to/ca.pem"
          value={syncTlsCaCertificatePath}
          onChange={(event) => onSetSyncTlsCaCertificatePath(event.target.value)}
          style={{ width: '260px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>TLS cert chain path</strong>
          <span>Node certificate chain PEM presented to peers</span>
        </div>
        <input
          className="setting-input"
          type="text"
          placeholder="/path/to/cert-chain.pem"
          value={syncTlsCertificateChainPath}
          onChange={(event) => onSetSyncTlsCertificateChainPath(event.target.value)}
          style={{ width: '260px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>TLS private key path</strong>
          <span>Private key PEM paired with the cert chain</span>
        </div>
        <input
          className="setting-input"
          type="text"
          placeholder="/path/to/key.pem"
          value={syncTlsPrivateKeyPath}
          onChange={(event) => onSetSyncTlsPrivateKeyPath(event.target.value)}
          style={{ width: '260px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Pinned peer SHA-256</strong>
          <span>Optional leaf certificate fingerprint pin (hex)</span>
        </div>
        <input
          className="setting-input"
          type="text"
          placeholder="64-char sha256"
          value={syncTlsPinnedPeerCertificateSha256}
          onChange={(event) => onSetSyncTlsPinnedPeerCertificateSha256(event.target.value)}
          style={{ width: '260px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Sync schema version</strong>
          <span>Local protocol/schema version advertised to peers</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={1}
          max={1000000}
          value={syncSchemaVersion}
          onChange={(event) => onSetSyncSchemaVersion(Math.max(1, Number(event.target.value) || 1))}
          style={{ width: '90px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Min supported schema</strong>
          <span>Reject peers below this schema version</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={1}
          max={1000000}
          value={syncMinSupportedSchemaVersion}
          onChange={(event) => onSetSyncMinSupportedSchemaVersion(Math.max(1, Number(event.target.value) || 1))}
          style={{ width: '90px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Allow schema downgrade</strong>
          <span>Permit negotiated downgrade to a lower compatible schema</span>
        </div>
        <button
          className={`setting-toggle${syncAllowSchemaDowngrade ? ' on' : ''}`}
          type="button"
          aria-label="Toggle sync schema downgrade"
          onClick={() => onSetSyncAllowSchemaDowngrade(!syncAllowSchemaDowngrade)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Peer probe enabled</strong>
          <span>Run lifecycle transport probes during peer refresh</span>
        </div>
        <button
          className={`setting-toggle${syncPeerProbeEnabled ? ' on' : ''}`}
          type="button"
          aria-label="Toggle sync peer probe enabled"
          onClick={() => onSetSyncPeerProbeEnabled(!syncPeerProbeEnabled)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Peer probe interval (ms)</strong>
          <span>Minimum interval between probe attempts for the same peer</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={100}
          max={300000}
          value={syncPeerProbeIntervalMs}
          onChange={(event) => onSetSyncPeerProbeIntervalMs(Number(event.target.value) || 0)}
          style={{ width: '110px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Peer probe timeout (ms)</strong>
          <span>Timeout used for per-attempt transport handshake probes</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={50}
          max={60000}
          value={syncPeerProbeTimeoutMs}
          onChange={(event) => onSetSyncPeerProbeTimeoutMs(Number(event.target.value) || 0)}
          style={{ width: '110px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Max probes per refresh</strong>
          <span>Bound probe work per refresh cycle to avoid bursts</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={1}
          max={64}
          value={syncPeerProbeMaxPerRefresh}
          onChange={(event) => onSetSyncPeerProbeMaxPerRefresh(Number(event.target.value) || 0)}
          style={{ width: '90px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Fail closed on probe failures</strong>
          <span>Mark peers unreachable after repeated failed probes</span>
        </div>
        <button
          className={`setting-toggle${syncPeerProbeFailClosed ? ' on' : ''}`}
          type="button"
          aria-label="Toggle sync peer probe fail closed"
          onClick={() => onSetSyncPeerProbeFailClosed(!syncPeerProbeFailClosed)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Fail-closed threshold</strong>
          <span>Consecutive probe failures required before fail-closed state applies</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={1}
          max={1000}
          value={syncPeerProbeFailClosedFailures}
          onChange={(event) => onSetSyncPeerProbeFailClosedFailures(Number(event.target.value) || 0)}
          style={{ width: '90px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Consistency model</strong>
        </div>
        <div style={{ fontSize: '11.5px', color: 'var(--text-secondary)', lineHeight: 1.5 }}>
          <div style={{ display: 'flex', gap: '0.4rem', alignItems: 'baseline' }}>
            <span style={{ color: 'var(--text)', fontWeight: 500 }}>Raft consensus</span>
            <span>accounts, settings, API keys</span>
          </div>
          <div style={{ display: 'flex', gap: '0.4rem', alignItems: 'baseline' }}>
            <span style={{ color: 'var(--text)', fontWeight: 500 }}>CRDT merge</span>
            <span>usage, sessions, IP allowlist</span>
          </div>
        </div>
      </div>
      <div className="setting-row" style={{ borderBottom: 'none' }}>
        <div className="setting-label">
          <strong>Cluster status</strong>
        </div>
        <div style={{ display: 'grid', gap: '0.3rem', fontSize: '11.5px', textAlign: 'right' }}>
          <div style={{ display: 'flex', gap: '0.8rem', alignItems: 'center', justifyContent: 'flex-end' }}>
            <span style={{ color: 'var(--accent)', fontWeight: 500 }}>
              {clusterStatus.enabled ? (clusterStatus.role === 'standalone' ? 'Standalone' : clusterStatus.role) : 'disabled'}
            </span>
            <span style={{ color: 'var(--text-secondary)' }}>Term {clusterStatus.term}</span>
            <span style={{ color: 'var(--text-secondary)' }}>Commit #{clusterStatus.commit_index}</span>
          </div>
          <div style={{ display: 'flex', gap: '0.8rem', alignItems: 'center', justifyContent: 'flex-end' }}>
            <span style={{ color: 'var(--ok)' }}>
              {connectedPeers}/{clusterStatus.peers.length} peers connected
            </span>
            {unreachablePeers > 0 ? <span style={{ color: 'var(--danger)' }}>{unreachablePeers} unreachable</span> : null}
            <span style={{ color: 'var(--text-secondary)' }}>Last sync: {formatLastSync(clusterStatus.last_sync_at)}</span>
            <span style={{ color: 'var(--text-secondary)' }}>Journal: {clusterStatus.journal_entries}</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'flex-end', color: 'var(--text-secondary)' }}>
            <span>Detailed lag, ingress, and peer metrics are in View topology → Stats.</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'flex-end', gap: '0.5rem' }}>
            <button className="btn-secondary" type="button" onClick={onOpenSyncTopology}>
              View topology
            </button>
            <button className="btn-secondary" type="button" onClick={onTriggerSyncNow}>
              Trigger sync now
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
