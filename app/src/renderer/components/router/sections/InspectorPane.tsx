import type { Account, RouteMetrics, RouteRow, RoutingMode } from '../../../shared/types';

interface InspectorPaneProps {
  inspectorOpen: boolean;
  selectedRoute: RouteRow;
  selectedRouteAccount: Account;
  selectedMetric: RouteMetrics | undefined;
  routingModes: RoutingMode[];
  formatNumber: (value: number) => string;
  onCloseInspector: () => void;
}

export function InspectorPane({
  inspectorOpen,
  selectedRoute,
  selectedRouteAccount,
  selectedMetric,
  routingModes,
  formatNumber,
  onCloseInspector,
}: InspectorPaneProps) {
  const strategyId =
    typeof selectedRoute.routingStrategy === 'string' && selectedRoute.routingStrategy.trim() !== ''
      ? selectedRoute.routingStrategy.trim()
      : null;
  const selectedScore =
    typeof selectedRoute.routingScore === 'number' && Number.isFinite(selectedRoute.routingScore)
      ? selectedRoute.routingScore
      : selectedMetric && Number.isFinite(selectedMetric.score)
        ? selectedMetric.score
        : null;
  const strategyMode = strategyId ? routingModes.find((mode) => mode.id === strategyId) : undefined;
  const strategyLabel = strategyMode?.label ?? strategyId ?? 'unknown';
  const strategyFormula = strategyMode?.formula ?? 'not captured';

  return (
    <aside className="pane detail-pane" id="detailPane">
      <header className="section-header">
        <div>
          <p className="eyebrow">Inspector</p>
          <h2>Route details</h2>
        </div>
        <button className="dialog-close" id="closeInspector" type="button" aria-label="Close inspector" onClick={onCloseInspector}>
          &times;
        </button>
      </header>
      <div className="pane-body">
        {!inspectorOpen ? (
          <div className="empty-state">Select a route to inspect routing details.</div>
        ) : (
          <div className="detail-stack" id="inspector">
            <section className="detail-section">
              <div className="detail-heading">
                <strong>Selected route</strong>
                <span>{selectedRoute.protocol}</span>
              </div>
              <div className="key-grid">
                <div className="key-row"><span className="key-label">Request</span><span className="key-value mono">{selectedRoute.id}</span></div>
                <div className="key-row"><span className="key-label">Session</span><span className="key-value mono">{selectedRoute.sessionId}</span></div>
                <div className="key-row"><span className="key-label">Model</span><span className="key-value">{selectedRoute.model}</span></div>
                <div className="key-row"><span className="key-label">Account</span><span className="key-value">{selectedRouteAccount.name}</span></div>
                <div className="key-row"><span className="key-label">Latency</span><span className="key-value">{selectedRoute.latency} ms</span></div>
                <div className="key-row"><span className="key-label">Score</span><span className="key-value">{selectedScore !== null ? selectedScore.toFixed(3) : '∞'}</span></div>
                <div className="key-row"><span className="key-label">Affinity</span><span className="key-value">{selectedRoute.sticky ? 'sticky reuse' : 'new allocation'}</span></div>
                <div className="key-row"><span className="key-label">Result</span><span className="key-value">{selectedRoute.status === 'ok' ? 'served' : selectedRoute.status === 'warn' ? 'fallback applied' : 'needs inspection'}</span></div>
              </div>
            </section>

            <section className="detail-section">
              <div className="detail-heading">
                <strong>Account headroom</strong>
                <span>{selectedRouteAccount.note}</span>
              </div>
              <div className="summary-grid">
                <div><span>Plan</span><strong>{selectedRouteAccount.plan}</strong></div>
                <div>
                  <span>Requests (24h)</span>
                  <strong>{selectedRouteAccount.telemetryBacked ? formatNumber(selectedRouteAccount.routed24h) : '—'}</strong>
                </div>
                <div><span>Inflight</span><strong>{selectedRouteAccount.telemetryBacked ? selectedRouteAccount.inflight : '—'}</strong></div>
                <div><span>Sticky hit</span><strong>{selectedRouteAccount.telemetryBacked ? `${selectedRouteAccount.stickyHit}%` : '—'}</strong></div>
              </div>
            </section>

            <section className="detail-section">
              <div className="detail-heading">
                <strong>Dispatch strategy</strong>
                <span>{strategyLabel}</span>
              </div>
              <div className="key-grid">
                <div className="key-row"><span className="key-label">Strategy ID</span><span className="key-value mono">{strategyId ?? 'unknown'}</span></div>
                <div className="key-row"><span className="key-label">Queue pressure</span><span className="key-value">{selectedMetric?.qNorm.toFixed(2) ?? '-'}</span></div>
                <div className="key-row"><span className="key-label">Latency pressure</span><span className="key-value">{selectedMetric?.lNorm.toFixed(2) ?? '-'}</span></div>
                <div className="key-row"><span className="key-label">Headroom</span><span className="key-value">{selectedMetric?.h.toFixed(2) ?? '-'}</span></div>
                <div className="key-row"><span className="key-label">Cooldown flag</span><span className="key-value">{selectedMetric?.c ?? '-'}</span></div>
              </div>
            </section>
          </div>
        )}
      </div>
    </aside>
  );
}
