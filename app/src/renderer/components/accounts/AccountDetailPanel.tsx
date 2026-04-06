import type { Account } from '../../shared/types';

interface AccountDetailPanelProps {
  selectedAccountDetail: Account | null;
  accountUsage24h: {
    requests: number;
    tokens: number;
    costUsd: number;
    failovers: number;
  };
  stableSparklinePercents: (key: string, currentPct: number) => number[];
  formatNumber: (value: number) => string;
  isRefreshingUsageTelemetry: boolean;
  isRefreshingToken: boolean;
  trafficNowMs: number;
  trafficActiveWindowMs: number;
  onRefreshUsageTelemetry: () => void;
  onRefreshToken: () => void;
  onPauseAccount: () => void;
  onReactivateAccount: () => void;
  onDeleteAccount: () => void;
}

const DAY_SECONDS = 24 * 60 * 60;
const MAX_UNIX_SECONDS_BEFORE_MS = 10_000_000_000;

interface UsageWindow {
  source: 'primary' | 'secondary';
  usedPercent: number;
  windowSeconds: number | null;
  resetAtMs: number | null;
}

interface UsageRow {
  key: 'short' | 'weekly';
  source: UsageWindow['source'];
  label: string;
  trendLabel: string;
  usedPercent: number;
  resetAtMs: number | null;
}

function sparkline(bars: number[]) {
  return bars.map((pct, index) => (
    <div key={index} className="spark-bar" style={{ height: `${Math.max(2, pct * 0.24)}px` }} />
  ));
}

function clampPercent(value: number): number {
  if (!Number.isFinite(value)) {
    return 0;
  }
  return Math.max(0, Math.min(100, Math.round(value)));
}

function normalizeResetAtMs(value: number | null | undefined): number | null {
  if (typeof value !== 'number' || !Number.isFinite(value) || value <= 0) {
    return null;
  }
  if (value < MAX_UNIX_SECONDS_BEFORE_MS) {
    return Math.trunc(value * 1000);
  }
  return Math.trunc(value);
}

function usageWindowFromAccount(detail: Account, source: UsageWindow['source']): UsageWindow | null {
  const hasQuota =
    source === 'primary'
      ? detail.hasPrimaryQuota === true || (detail.hasPrimaryQuota === undefined && detail.telemetryBacked)
      : detail.hasSecondaryQuota === true;
  if (!hasQuota) {
    return null;
  }

  const usedPercent = source === 'primary' ? clampPercent(detail.quotaPrimary) : clampPercent(detail.quotaSecondary);
  const rawWindowSeconds = source === 'primary' ? detail.quotaPrimaryWindowSeconds : detail.quotaSecondaryWindowSeconds;
  const windowSeconds =
    typeof rawWindowSeconds === 'number' && Number.isFinite(rawWindowSeconds) && rawWindowSeconds > 0
      ? Math.max(1, Math.trunc(rawWindowSeconds))
      : null;
  const rawResetAt = source === 'primary' ? detail.quotaPrimaryResetAtMs : detail.quotaSecondaryResetAtMs;

  return {
    source,
    usedPercent,
    windowSeconds,
    resetAtMs: normalizeResetAtMs(rawResetAt),
  };
}

function findWeeklyWindow(windows: UsageWindow[], detail: Account): UsageWindow | null {
  const explicitWeekly = windows.find((window) => window.windowSeconds !== null && window.windowSeconds >= DAY_SECONDS);
  if (explicitWeekly) {
    return explicitWeekly;
  }
  if (detail.plan === 'free') {
    return windows[0] ?? null;
  }
  return windows.find((window) => window.source === 'secondary') ?? null;
}

function findShortWindow(windows: UsageWindow[], detail: Account): UsageWindow | null {
  if (detail.plan === 'free') {
    return null;
  }
  const explicitShort = windows.find((window) => window.windowSeconds !== null && window.windowSeconds < DAY_SECONDS);
  if (explicitShort) {
    return explicitShort;
  }
  return windows.find((window) => window.source === 'primary') ?? null;
}

function usageRowsFromAccount(detail: Account): UsageRow[] {
  const windows = [usageWindowFromAccount(detail, 'primary'), usageWindowFromAccount(detail, 'secondary')].filter(
    (value): value is UsageWindow => value !== null,
  );
  if (windows.length === 0) {
    return [];
  }

  const rows: UsageRow[] = [];
  const shortWindow = findShortWindow(windows, detail);
  if (detail.plan !== 'free') {
    if (shortWindow) {
      rows.push({
        key: 'short',
        source: shortWindow.source,
        label: '5h remaining',
        trendLabel: '5h',
        usedPercent: shortWindow.usedPercent,
        resetAtMs: shortWindow.resetAtMs,
      });
    }
  }

  const weeklyWindow = findWeeklyWindow(windows, detail);
  const weeklyDuplicatesShort =
    weeklyWindow !== null &&
    shortWindow !== null &&
    weeklyWindow.source === shortWindow.source &&
    weeklyWindow.usedPercent === shortWindow.usedPercent &&
    weeklyWindow.windowSeconds === shortWindow.windowSeconds;
  if (weeklyWindow && !weeklyDuplicatesShort) {
    rows.push({
      key: 'weekly',
      source: weeklyWindow.source,
      label: 'Weekly remaining',
      trendLabel: 'Weekly',
      usedPercent: weeklyWindow.usedPercent,
      resetAtMs: weeklyWindow.resetAtMs,
    });
  }

  if (rows.length === 0) {
    const fallbackWindow = windows[0];
    rows.push({
      key: 'weekly',
      source: fallbackWindow.source,
      label: 'Weekly remaining',
      trendLabel: 'Weekly',
      usedPercent: fallbackWindow.usedPercent,
      resetAtMs: fallbackWindow.resetAtMs,
    });
  }

  return rows;
}

function usageResetLabel(resetAtMs: number | null): string {
  if (resetAtMs === null) {
    return 'Reset time unavailable';
  }
  const remainingMs = resetAtMs - Date.now();
  if (remainingMs <= 0) {
    return 'Resetting soon';
  }

  const totalMinutes = Math.floor(remainingMs / 60_000);
  if (totalMinutes < 1) {
    return 'Resets in <1m';
  }

  const days = Math.floor(totalMinutes / (24 * 60));
  const hours = Math.floor((totalMinutes % (24 * 60)) / 60);
  const minutes = totalMinutes % 60;
  if (days > 0) {
    return `Resets in ${days}d ${hours}h`;
  }
  if (hours > 0) {
    return `Resets in ${hours}h ${minutes}m`;
  }
  return `Resets in ${minutes}m`;
}

export function AccountDetailPanel({
  selectedAccountDetail,
  accountUsage24h,
  stableSparklinePercents,
  formatNumber,
  isRefreshingUsageTelemetry,
  isRefreshingToken,
  trafficNowMs,
  trafficActiveWindowMs,
  onRefreshUsageTelemetry,
  onRefreshToken,
  onPauseAccount,
  onReactivateAccount,
  onDeleteAccount,
}: AccountDetailPanelProps) {
  const detail = selectedAccountDetail;
  const hasTelemetry = detail?.telemetryBacked ?? false;
  const detailUpActive = detail != null &&
    (detail.trafficLastUpAtMs ?? 0) > 0 &&
    trafficNowMs - (detail.trafficLastUpAtMs ?? 0) <= trafficActiveWindowMs;
  const detailDownActive = detail != null &&
    (detail.trafficLastDownAtMs ?? 0) > 0 &&
    trafficNowMs - (detail.trafficLastDownAtMs ?? 0) <= trafficActiveWindowMs;
  const usageRows = detail ? usageRowsFromAccount(detail) : [];
  const tokenRefreshRequired = detail?.usageRefreshStatus === 'auth_required' || detail?.needsTokenRefresh === true;
  const tokenStateClass = tokenRefreshRequired ? 'token-warn' : 'token-ok';
  const accessTokenStateLabel = tokenRefreshRequired ? 'refresh required' : 'stored encrypted locally';
  const refreshTokenStateLabel = tokenRefreshRequired
    ? 'refresh recommended'
    : 'stored encrypted locally (auto-refresh on 401)';
  const idTokenStateLabel = tokenRefreshRequired ? 'pending refresh' : 'stored encrypted locally';

  return (
    <div className="accounts-detail" id="accountDetailPanel">
      <header className="section-header">
        <div>
          <p className="eyebrow">Detail</p>
          <h2>{detail?.name ?? 'Select an account'}</h2>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
          {detail && (
            <span className={`traffic-indicator detail-traffic${detailUpActive || detailDownActive ? ' active' : ''}`} aria-hidden="true">
              <span className={`traffic-arrow up${detailUpActive ? ' active' : ''}`}>↑</span>
              <span className={`traffic-arrow down${detailDownActive ? ' active' : ''}`}>↓</span>
            </span>
          )}
          <span style={{ fontSize: '11px', color: 'var(--text-tertiary)', fontFamily: "'SF Mono',ui-monospace,monospace" }}>{detail?.id ?? ''}</span>
        </div>
      </header>
      <div className="detail-content">
        {!detail ? (
          <div className="empty-detail">
            <strong>Select an account</strong>
            <span>Choose an account from the list to view details</span>
          </div>
        ) : (
          <>
            <div className="detail-card">
              <div className="detail-card-header">
                <h4>Usage</h4>
                <button
                  className="btn-secondary"
                  type="button"
                  onClick={onRefreshUsageTelemetry}
                  disabled={isRefreshingUsageTelemetry}
                >
                  {isRefreshingUsageTelemetry ? 'Refreshing...' : 'Refresh usage'}
                </button>
              </div>
              {usageRows.length === 0 ? (
                <div className="usage-bar-row">
                  <div className="usage-label-row">
                    <span>Usage telemetry</span>
                    <span>—</span>
                  </div>
                  <div className="usage-bar">
                    <div className="usage-fill" style={{ width: '0%' }} />
                  </div>
                  <span className="usage-reset">No DB usage telemetry yet</span>
                </div>
              ) : (
                usageRows.map((row) => {
                  const remainingPercent = clampPercent(100 - row.usedPercent);
                  return (
                    <div key={row.key} className="usage-bar-row">
                      <div className="usage-label-row">
                        <span>{row.label}</span>
                        <span>{hasTelemetry ? `${remainingPercent}%` : '—'}</span>
                      </div>
                      <div className="usage-bar">
                        <div className="usage-fill" style={{ width: `${hasTelemetry ? remainingPercent : 0}%` }} />
                      </div>
                      <span className="usage-reset">{hasTelemetry ? usageResetLabel(row.resetAtMs) : 'No DB usage telemetry yet'}</span>
                    </div>
                  );
                })
              )}
            </div>

            <div className="detail-card">
              <h4>7-day usage trend</h4>
              {hasTelemetry && usageRows.length > 0 ? (
                <>
                  {usageRows.map((row) => (
                    <div key={row.key} className="trend-row">
                      <span className="trend-label">{row.trendLabel}</span>
                      <div className="sparkline">
                        {sparkline(stableSparklinePercents(`${detail.id}-${row.source === 'primary' ? 'p' : 's'}`, row.usedPercent))}
                      </div>
                    </div>
                  ))}
                </>
              ) : (
                <div className="empty-detail">
                  <span>No historical usage telemetry in DB yet</span>
                </div>
              )}
            </div>

            <div className="detail-card">
              <h4>Request usage</h4>
              <div className="request-stats">
                <div className="request-stat">
                  <span>Requests</span>
                  {formatNumber(accountUsage24h.requests)}
                </div>
                <div className="request-stat">
                  <span>Tokens</span>
                  {formatNumber(accountUsage24h.tokens)}
                </div>
                <div className="request-stat">
                  <span>Cost</span>{`$${accountUsage24h.costUsd.toFixed(2)}`}
                </div>
                <div className="request-stat">
                  <span>Failovers</span>
                  {formatNumber(accountUsage24h.failovers)}
                </div>
              </div>
            </div>

            <div className="detail-card">
              <div className="detail-card-header">
                <h4>Token status</h4>
                {tokenRefreshRequired ? (
                  <button
                    className="btn-secondary"
                    type="button"
                    onClick={onRefreshToken}
                    disabled={isRefreshingToken}
                  >
                    {isRefreshingToken ? 'Refreshing token...' : 'Refresh token'}
                  </button>
                ) : null}
              </div>
              <dl className="token-grid">
                <dt>Access</dt>
                <dd className={tokenStateClass}>{accessTokenStateLabel}</dd>
                <dt>Refresh</dt>
                <dd className={tokenStateClass}>{refreshTokenStateLabel}</dd>
                <dt>ID token</dt>
                <dd className={tokenStateClass}>{idTokenStateLabel}</dd>
              </dl>
            </div>

            <div className="account-actions">
              {detail.state === 'paused' ? (
                <button className="dock-btn accent" type="button" onClick={onReactivateAccount}>
                  Resume
                </button>
              ) : (
                <button className="dock-btn" type="button" onClick={onPauseAccount}>
                  Pause
                </button>
              )}
              {detail.state === 'deactivated' && (
                <button className="dock-btn" type="button">
                  Re-authenticate
                </button>
              )}
              <button className="btn-danger" type="button" onClick={onDeleteAccount}>
                Delete
              </button>
            </div>
          </>
        )}
      </div>
    </div>
  );
}
