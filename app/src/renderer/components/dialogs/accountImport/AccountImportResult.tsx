import type { SqlImportApplyResponse } from '../../../shared/types';

interface AccountImportResultProps {
  result: SqlImportApplyResponse;
}

export function AccountImportResult({ result }: AccountImportResultProps) {
  return (
    <section className="account-import-result">
      <div className="account-import-preview-header">
        <strong>Import result</strong>
        <span>Completed</span>
      </div>
      <div className="account-import-result-grid">
        <div className="account-import-kpi">
          <span>Scanned</span>
          <strong>{result.totals.scanned}</strong>
        </div>
        <div className="account-import-kpi">
          <span>Inserted</span>
          <strong>{result.totals.inserted}</strong>
        </div>
        <div className="account-import-kpi">
          <span>Updated</span>
          <strong>{result.totals.updated}</strong>
        </div>
        <div className="account-import-kpi">
          <span>Skipped</span>
          <strong>{result.totals.skipped}</strong>
        </div>
        <div className="account-import-kpi">
          <span>Invalid</span>
          <strong>{result.totals.invalid}</strong>
        </div>
        <div className="account-import-kpi">
          <span>Failed</span>
          <strong>{result.totals.failed}</strong>
        </div>
      </div>
      {result.warnings.length > 0 && (
        <ul className="account-import-warning-list">
          {result.warnings.map((warning, index) => (
            <li key={`${warning}-${index}`}>{warning}</li>
          ))}
        </ul>
      )}
    </section>
  );
}
