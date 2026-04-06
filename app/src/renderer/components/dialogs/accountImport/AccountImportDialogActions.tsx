interface AccountImportDialogActionsProps {
  stage: 'idle' | 'scanning' | 'ready' | 'importing' | 'done' | 'error';
  importEnabled: boolean;
  rescanEnabled: boolean;
  onCancel: () => void;
  onRescan: () => void;
  onImport: () => void;
}

export function AccountImportDialogActions({
  stage,
  importEnabled,
  rescanEnabled,
  onCancel,
  onRescan,
  onImport,
}: AccountImportDialogActionsProps) {
  if (stage === 'done') {
    return (
      <div className="dialog-actions">
        <button className="dock-btn" type="button" onClick={onCancel}>
          Close
        </button>
      </div>
    );
  }

  return (
    <div className="dialog-actions">
      <button className="dock-btn" type="button" onClick={onCancel}>
        Cancel
      </button>
      <button className="dock-btn" type="button" disabled={!rescanEnabled} onClick={onRescan}>
        Rescan
      </button>
      <button className="dock-btn accent" type="button" disabled={!importEnabled} onClick={onImport}>
        {stage === 'importing' ? 'Importing…' : 'Import delta'}
      </button>
    </div>
  );
}
