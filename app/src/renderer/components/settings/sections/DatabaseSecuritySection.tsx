import { useState } from 'react';
import { useTightropeService } from '../../../state/context';

export function DatabaseSecuritySection() {
  const service = useTightropeService();
  const [currentPassphrase, setCurrentPassphrase] = useState('');
  const [nextPassphrase, setNextPassphrase] = useState('');
  const [confirmation, setConfirmation] = useState('');
  const [saving, setSaving] = useState(false);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [successMessage, setSuccessMessage] = useState<string | null>(null);

  async function submitChange(): Promise<void> {
    setErrorMessage(null);
    setSuccessMessage(null);

    if (currentPassphrase.length === 0) {
      setErrorMessage('Current passphrase is required.');
      return;
    }
    if (nextPassphrase.length < 8) {
      setErrorMessage('New passphrase must be at least 8 characters.');
      return;
    }
    if (nextPassphrase !== confirmation) {
      setErrorMessage('New passphrase and confirmation do not match.');
      return;
    }
    if (nextPassphrase === currentPassphrase) {
      setErrorMessage('New passphrase must differ from current passphrase.');
      return;
    }

    setSaving(true);
    try {
      await service.changeDatabasePassphraseRequest(currentPassphrase, nextPassphrase);
      setSuccessMessage('Database passphrase changed for this running session.');
      setCurrentPassphrase('');
      setNextPassphrase('');
      setConfirmation('');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to change database passphrase.';
      setErrorMessage(message);
    } finally {
      setSaving(false);
    }
  }

  return (
    <div className="settings-group">
      <div className="settings-group-header">
        <h3>Database Security</h3>
        <p>Rotate the database password used for your local `store.db` and encrypted account token payloads.</p>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Change database password</strong>
          <span>You must provide the current passphrase. Keep the new passphrase safe, recovery is not possible.</span>
        </div>
        <div className="setting-inline-fields">
          <input
            className="setting-select setting-input--password"
            type="password"
            placeholder="Current"
            value={currentPassphrase}
            onChange={(event) => setCurrentPassphrase(event.target.value)}
            autoComplete="current-password"
            disabled={saving}
          />
          <input
            className="setting-select setting-input--password"
            type="password"
            placeholder="New"
            value={nextPassphrase}
            onChange={(event) => setNextPassphrase(event.target.value)}
            autoComplete="new-password"
            disabled={saving}
          />
          <input
            className="setting-select setting-input--password"
            type="password"
            placeholder="Confirm"
            value={confirmation}
            onChange={(event) => setConfirmation(event.target.value)}
            autoComplete="new-password"
            disabled={saving}
          />
          <button className="dock-btn accent" type="button" onClick={() => void submitChange()} disabled={saving}>
            {saving ? 'Updating…' : 'Change Password'}
          </button>
        </div>
      </div>
      {(errorMessage || successMessage) && (
        <div className={`setting-feedback${errorMessage ? ' error' : ' success'}`}>
          {errorMessage ?? successMessage}
        </div>
      )}
    </div>
  );
}
