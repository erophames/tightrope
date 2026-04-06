import { useAccountsContext, useNavigationContext, useRouterDerivedContext, useRuntimeContext } from '../../state/context';

export function RouterToolbar() {
  const navigation = useNavigationContext();
  const accounts = useAccountsContext();
  const derived = useRouterDerivedContext();
  const runtime = useRuntimeContext();
  const eligibleCount = Array.from(derived.metrics.values()).filter((metric) => metric.capability).length;

  if (navigation.currentPage !== 'router') return null;

  return (
    <header className="tool-strip">
      <div className="tool-context">
        <div className="router-state" data-state={derived.routerState}>
          <span className="state-dot" aria-hidden="true" />
          <div className="router-state-copy">
            <strong>Router {derived.routerState}</strong>
            <span>
              {derived.routerState === 'stopped'
                ? 'Waiting for backend start'
                : derived.routerState === 'paused'
                  ? 'New traffic paused, sessions visible'
                  : derived.routerState === 'degraded'
                    ? 'Health checks need attention'
                    : ' Healthy'}
            </span>
          </div>
        </div>
        <div className="tool-chip">
          <span>Bind</span>
          <strong className="mono">{runtime.runtimeState.bind}</strong>
        </div>
        <div className="tool-chip">
          <span>Strategy</span>
          <strong>{derived.modeLabel}</strong>
        </div>
        <div className="tool-chip">
          <span>Eligible</span>
          <strong>{`${eligibleCount}/${accounts.accounts.length}`}</strong>
        </div>
      </div>
      <div className="tool-actions">
        <button className="tool-btn" id="startRouter" type="button" onClick={() => runtime.setRuntimeAction('start')}>
          Start
        </button>
        <button className="tool-btn" id="restartRouter" type="button" onClick={() => runtime.setRuntimeAction('restart')}>
          Restart
        </button>
        <button className="tool-btn accent" id="pauseRouter" type="button" onClick={runtime.toggleRoutePause}>
          {runtime.runtimeState.pausedRoutes ? 'Resume Routes' : 'Pause Routes'}
        </button>
        <span className="tool-sep" aria-hidden="true" />
        <button className="tool-btn" id="openBackendDialog" type="button" onClick={runtime.openBackendDialog}>
          Backend
        </button>
        <button className="tool-btn" id="openAuthDialog" type="button" onClick={runtime.openAuthDialog}>
          OAuth
        </button>
      </div>
    </header>
  );
}
