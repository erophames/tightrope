export function AccountImportHero() {
  return (
    <section className="account-import-hero">
      <p className="account-import-eyebrow">Data import</p>
      <strong>Import accounts from a SQLite workspace snapshot</strong>
      <span>
        Drop a <code className="mono">.sqlite</code> or <code className="mono">.db</code> file, inspect account and login deltas, then
        apply only reviewed changes.
      </span>
      <div className="account-import-flow">
        <span>1. Select source DB</span>
        <span>2. Review account deltas</span>
        <span>3. Import selected rows</span>
      </div>
    </section>
  );
}
