import { useRuntimeContext } from '../../state/context';
import { currentRouterState } from '../../state/logic';

export function BackendDialog() {
  const runtime = useRuntimeContext();

  if (!runtime.backendDialogOpen) return null;
  const runtimeState = runtime.runtimeState;
  const routerState = currentRouterState(runtimeState);

  return (
    <dialog open id="backendDialog" onClick={(event) => event.currentTarget === event.target && runtime.closeBackendDialog()}>
      <header className="dialog-header">
        <h3>Backend</h3>
        <button className="dialog-close" type="button" aria-label="Close" onClick={runtime.closeBackendDialog}>
          &times;
        </button>
      </header>
      <div className="dialog-body">
        <div className="summary-grid">
          <div>
            <span>Router</span>
            <strong>{routerState}</strong>
          </div>
          <div>
            <span>Backend</span>
            <strong>{runtimeState.backend}</strong>
          </div>
          <div>
            <span>Health</span>
            <strong>{runtimeState.health}</strong>
          </div>
          <div>
            <span>Restart</span>
            <strong>{runtimeState.autoRestart ? 'armed' : 'manual'}</strong>
          </div>
        </div>
        <div className="button-row">
          <button className="dock-btn" type="button" onClick={() => runtime.setRuntimeAction('start')}>
            Start
          </button>
          <button className="dock-btn" type="button" onClick={() => runtime.setRuntimeAction('restart')}>
            Restart
          </button>
          <button className="dock-btn" type="button" onClick={() => runtime.setRuntimeAction('stop')}>
            Stop
          </button>
          <button className="dock-btn accent" type="button" onClick={runtime.toggleAutoRestart}>
            {runtimeState.autoRestart ? 'Disable Auto-Restart' : 'Enable Auto-Restart'}
          </button>
        </div>
        <div className="footnote">
          <span className="mono">{runtimeState.bind}</span> loopback • {runtimeState.pausedRoutes ? 'traffic paused' : 'accepting traffic'}
        </div>
      </div>
    </dialog>
  );
}
