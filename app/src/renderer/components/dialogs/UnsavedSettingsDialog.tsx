import { useNavigationContext } from '../../state/context';

export function UnsavedSettingsDialog() {
  const navigation = useNavigationContext();
  const open = navigation.settingsLeaveDialogOpen;
  const busy = navigation.settingsSaveInFlight;

  if (!open) {
    return null;
  }

  return (
    <dialog
      open
      id="unsavedSettingsDialog"
      onClick={(event) => {
        if (event.currentTarget === event.target && !busy) {
          navigation.closeSettingsLeaveDialog();
        }
      }}
    >
      <header className="dialog-header">
        <h3>Unsaved Settings</h3>
      </header>
      <div className="dialog-body">
        <p className="unsaved-settings-copy">
          You have unsaved changes in Settings. Save before leaving this page?
        </p>
        <div className="dialog-actions">
          <button className="dock-btn" type="button" disabled={busy} onClick={navigation.closeSettingsLeaveDialog}>
            Cancel
          </button>
          <button className="dock-btn" type="button" disabled={busy} onClick={navigation.discardSettingsAndNavigate}>
            Discard
          </button>
          <button className="dock-btn accent" type="button" disabled={busy} onClick={() => void navigation.saveSettingsAndNavigate()}>
            {busy ? 'Saving…' : 'Save'}
          </button>
        </div>
      </div>
    </dialog>
  );
}
