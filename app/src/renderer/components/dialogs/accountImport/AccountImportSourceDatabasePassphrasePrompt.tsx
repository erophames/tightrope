interface AccountImportSourceDatabasePassphrasePromptProps {
  value: string;
  required: boolean;
  disabled: boolean;
  onChange: (value: string) => void;
}

export function AccountImportSourceDatabasePassphrasePrompt({
  value,
  required,
  disabled,
  onChange,
}: AccountImportSourceDatabasePassphrasePromptProps) {
  return (
    <section className="account-import-source-key">
      <div className="account-import-source-key-header">
        <strong>Source database password</strong>
        {required && <span>Required for encrypted source database</span>}
      </div>
      <input
        className="dock-input account-import-source-key-input"
        type="password"
        spellCheck={false}
        autoComplete="off"
        placeholder="Enter source database password"
        value={value}
        disabled={disabled}
        onChange={(event) => onChange(event.target.value)}
      />
      <small>Enter the password for the imported database, then run Rescan.</small>
    </section>
  );
}
