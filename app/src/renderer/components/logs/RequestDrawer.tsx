import { useAccountsContext, useLogsContext, useRouterDerivedContext } from '../../state/context';
import { statusClass } from '../../state/logic';

export function RequestDrawer() {
  const accounts = useAccountsContext();
  const logs = useLogsContext();
  const derived = useRouterDerivedContext();
  const row = logs.drawerRow;
  const open = row !== null;
  if (!row) {
    return (
      <>
        <div className="drawer-backdrop" />
        <aside className="log-drawer" />
      </>
    );
  }

  const account = accounts.accounts.find((candidate) => candidate.id === row.accountId) ?? accounts.accounts[0];
  const accountName = account?.name ?? row.accountId ?? 'unassigned';
  const scored = accounts.accounts
    .map((candidate) => {
      const metric = derived.metrics.get(candidate.id);
      return {
        name: candidate.name,
        score: metric?.score ?? Infinity,
        picked: candidate.id === row.accountId,
      };
    })
    .sort((a, b) => a.score - b.score);

  return (
    <>
      <div className={`drawer-backdrop${open ? ' open' : ''}`} onClick={logs.closeDrawer} />
      <aside className={`log-drawer${open ? ' open' : ''}`}>
        <header className="section-header">
          <div>
            <p className="eyebrow">Request</p>
            <h2>{row.id}</h2>
          </div>
          <button className="dialog-close" type="button" aria-label="Close" onClick={logs.closeDrawer}>
            &times;
          </button>
        </header>
        <div className="drawer-body">
          <div className="drawer-section">
            <div className="drawer-section-header">Request</div>
            <dl className="drawer-kv">
              <dt>ID</dt>
              <dd>{row.id}</dd>
              {row.requestedAt ? (
                <>
                  <dt>Requested At</dt>
                  <dd>{row.requestedAt}</dd>
                </>
              ) : null}
              <dt>Time</dt>
              <dd>{row.time}</dd>
              {row.method ? (
                <>
                  <dt>Method</dt>
                  <dd>{row.method}</dd>
                </>
              ) : null}
              {row.path ? (
                <>
                  <dt>Path</dt>
                  <dd>{row.path}</dd>
                </>
              ) : null}
              <dt>Protocol</dt>
              <dd>{row.protocol}</dd>
              <dt>Model</dt>
              <dd>{row.model}</dd>
              <dt>Account</dt>
              <dd>{accountName}</dd>
              <dt>Tokens</dt>
              <dd>{accounts.formatNumber(row.tokens)}</dd>
              <dt>Latency</dt>
              <dd>{row.latency} ms</dd>
              {typeof row.statusCode === 'number' ? (
                <>
                  <dt>Status Code</dt>
                  <dd>{row.statusCode}</dd>
                </>
              ) : null}
              <dt>Status</dt>
              <dd>
                <span className={`status-badge ${statusClass(row.status)}`}>{row.status}</span>
              </dd>
              {row.errorCode ? (
                <>
                  <dt>Error Code</dt>
                  <dd>{row.errorCode}</dd>
                </>
              ) : null}
              <dt>Session</dt>
              <dd>{row.sessionId}</dd>
            </dl>
          </div>

          <div className="drawer-section">
            <div className="drawer-section-header">Routing decision</div>
            <table className="scoring-table">
              <thead>
                <tr>
                  <th>Account</th>
                  <th>Score</th>
                </tr>
              </thead>
              <tbody>
                {scored.map((entry) => (
                  <tr key={entry.name} className={entry.picked ? 'picked' : ''}>
                    <td>
                      {entry.name}
                      {entry.picked ? ' ←' : ''}
                    </td>
                    <td style={{ fontFamily: "'SF Mono',ui-monospace,monospace", fontSize: '11px' }}>
                      {Number.isFinite(entry.score) ? entry.score.toFixed(4) : '∞'}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      </aside>
    </>
  );
}
