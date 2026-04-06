import { useAccountsContext, useLogsContext, useNavigationContext } from '../../state/context';
import { statusClass } from '../../state/logic';

export function LogsPage() {
  const navigation = useNavigationContext();
  const accounts = useAccountsContext();
  const logs = useLogsContext();

  if (navigation.currentPage !== 'logs') return null;

  return (
    <section className="logs-page page active" id="pageLogs" data-page="logs">
      <div className="logs-content">
        <header className="section-header">
          <div>
            <p className="eyebrow">Request</p>
            <h2>Logs</h2>
          </div>
        </header>
        <div className="table-wrap">
          <table>
            <thead>
              <tr>
                <th>Time</th>
                <th>Request</th>
                <th>Protocol</th>
                <th>Model</th>
                <th>Account</th>
                <th>Tokens</th>
                <th>Latency</th>
                <th>Status</th>
              </tr>
            </thead>
            <tbody>
              {logs.rows.map((row) => {
                const accountName = accounts.accounts.find((account) => account.id === row.accountId)?.name ?? row.accountId;
                const requestLabel = row.path ? `${row.method ?? 'POST'} ${row.path}` : row.id;
                return (
                  <tr key={row.id} className="route-row" style={{ cursor: 'pointer' }} onClick={() => logs.openDrawer(row.id)}>
                    <td>{row.time}</td>
                    <td className="mono">{requestLabel}</td>
                    <td>{row.protocol}</td>
                    <td className="model-cell">{row.model}</td>
                    <td className="account-cell">{accountName}</td>
                    <td>{accounts.formatNumber(row.tokens)}</td>
                    <td>{row.latency} ms</td>
                    <td>
                      <span className={`status-badge ${statusClass(row.status)}`}>{row.status}</span>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </div>
    </section>
  );
}
