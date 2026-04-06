import type { SqlImportPreviewResponse } from '../../../shared/types';

interface AccountImportSummaryProps {
  preview: SqlImportPreviewResponse;
}

function formatFileSize(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let value = bytes;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit += 1;
  }
  const precision = value >= 100 || unit === 0 ? 0 : value >= 10 ? 1 : 2;
  return `${value.toFixed(precision)} ${units[unit]}`;
}

function formatLastModified(lastModifiedMs: number): string {
  if (!Number.isFinite(lastModifiedMs) || lastModifiedMs <= 0) return 'unknown';
  return new Date(lastModifiedMs).toLocaleString();
}

export function AccountImportSummary({ preview }: AccountImportSummaryProps) {
  return (
    <section className="account-import-summary">
      <div className="account-import-kpis">
        <div className="account-import-kpi">
          <span>Total rows</span>
          <strong>{preview.totals.scanned}</strong>
        </div>
        <div className="account-import-kpi">
          <span>New</span>
          <strong>{preview.totals.newCount}</strong>
        </div>
        <div className="account-import-kpi">
          <span>Update</span>
          <strong>{preview.totals.updateCount}</strong>
        </div>
        <div className="account-import-kpi">
          <span>Skip</span>
          <strong>{preview.totals.skipCount}</strong>
        </div>
        <div className="account-import-kpi">
          <span>Invalid</span>
          <strong>{preview.totals.invalidCount}</strong>
        </div>
      </div>
      <div className="account-import-source">
        <div>
          <span>Source DB</span>
          <strong>{preview.source.fileName || '-'}</strong>
        </div>
        <div>
          <span>Last modified</span>
          <strong>{formatLastModified(preview.source.modifiedAtMs)}</strong>
        </div>
        <div>
          <span>File size</span>
          <strong>{formatFileSize(preview.source.sizeBytes)}</strong>
        </div>
        <div>
          <span>Schema</span>
          <strong>{preview.source.schemaFingerprint || '-'}</strong>
        </div>
      </div>
    </section>
  );
}
