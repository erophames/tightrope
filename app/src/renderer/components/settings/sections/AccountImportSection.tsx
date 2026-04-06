interface AccountImportSectionProps {
  importWithoutOverwrite: boolean;
  onSetImportWithoutOverwrite: (enabled: boolean) => void;
  onOpenImportDialog: () => void;
}

export function AccountImportSection({
  importWithoutOverwrite,
  onSetImportWithoutOverwrite,
  onOpenImportDialog,
}: AccountImportSectionProps) {
  return (
    <div className="settings-group">
      <div className="settings-group-header">
        <h3>Account Import</h3>
        <p>Load accounts and login credentials from a SQLite snapshot with a delta preview before import.</p>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Import source database</strong>
          <span>Open the import dialog to review account/login changes before applying.</span>
        </div>
        <button className="dock-btn accent" type="button" onClick={onOpenImportDialog}>
          Import SQLite DB
        </button>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Import without overwrite</strong>
          <span>Keep existing accounts when importing duplicates.</span>
        </div>
        <button
          className={`setting-toggle${importWithoutOverwrite ? ' on' : ''}`}
          type="button"
          aria-label="Toggle import without overwrite"
          onClick={() => onSetImportWithoutOverwrite(!importWithoutOverwrite)}
        />
      </div>
    </div>
  );
}
