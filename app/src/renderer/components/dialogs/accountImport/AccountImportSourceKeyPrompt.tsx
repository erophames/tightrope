interface AccountImportSourceKeyPromptProps {
  value: string;
  required: boolean;
  disabled: boolean;
  onChange: (value: string) => void;
}

export function AccountImportSourceKeyPrompt({
  value,
  required,
  disabled,
  onChange,
}: AccountImportSourceKeyPromptProps) {
  return (
    <section className="account-import-source-key">
      <div className="account-import-source-key-header">
        <strong>Source encryption key</strong>
        {required && <span>Required for encrypted source tokens</span>}
      </div>
      <input
        className="dock-input account-import-source-key-input mono"
        type="password"
        spellCheck={false}
        autoComplete="off"
        placeholder="Paste source Fernet key (base64)"
        value={value}
        disabled={disabled}
        onChange={(event) => onChange(event.target.value)}
      />
      <small>Enter the source key, then run Rescan to validate encrypted login tokens.</small>
    </section>
  );
}
