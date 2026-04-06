import type { SqlImportAction, SqlImportPreviewRow } from '../../../shared/types';

interface AccountImportPreviewTableProps {
  rows: SqlImportPreviewRow[];
  overrides: Record<string, SqlImportAction>;
  disableActions?: boolean;
  importWithoutOverwrite: boolean;
  onOverrideChange: (sourceRowId: string, action: SqlImportAction) => void;
}

function badgeClass(action: SqlImportAction): string {
  return `account-import-badge ${action}`;
}

function authLabel(row: SqlImportPreviewRow): string {
  const parts: string[] = [];
  if (row.hasAccessToken) parts.push('access');
  if (row.hasRefreshToken) parts.push('refresh');
  if (row.hasIdToken) parts.push('id');
  return parts.length > 0 ? parts.join(' + ') : 'none';
}

export function AccountImportPreviewTable({
  rows,
  overrides,
  disableActions = false,
  importWithoutOverwrite,
  onOverrideChange,
}: AccountImportPreviewTableProps) {
  return (
    <section className="account-import-preview">
      <div className="account-import-preview-header">
        <strong>Accounts and login details delta</strong>
        <span>{importWithoutOverwrite ? 'No-overwrite mode enabled' : 'Overwrite mode enabled'}</span>
      </div>
      <div className="account-import-table-wrap">
        <table className="account-import-table">
          <thead>
            <tr>
              <th>Email</th>
              <th>Provider</th>
              <th>Plan</th>
              <th>Detected auth</th>
              <th>Action</th>
              <th>Reason</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((row) => (
              <tr key={row.sourceRowId}>
                <td>{row.email ?? '—'}</td>
                <td>{row.provider ?? '—'}</td>
                <td>{row.planType ?? '—'}</td>
                <td>
                  <span className="account-import-auth">{authLabel(row)}</span>
                </td>
                <td>
                  {row.action === 'invalid' ? (
                    <span className={badgeClass('invalid')}>invalid</span>
                  ) : (
                    <select
                      className="account-import-action-select"
                      value={overrides[row.sourceRowId] ?? row.action}
                      onChange={(event) => onOverrideChange(row.sourceRowId, event.target.value as SqlImportAction)}
                      disabled={disableActions}
                    >
                      <option value="new">new</option>
                      <option value="update">update</option>
                      <option value="skip">skip</option>
                    </select>
                  )}
                </td>
                <td>
                  <span className={badgeClass(overrides[row.sourceRowId] ?? row.action)}>
                    {overrides[row.sourceRowId] ?? row.action}
                  </span>
                  <span className="account-import-reason">{row.reason}</span>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}
