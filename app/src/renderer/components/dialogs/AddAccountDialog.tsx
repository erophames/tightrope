import { CheckCircle2 } from 'lucide-react';
import type { AddAccountStep } from '../../shared/types';
import { useAccountsContext } from '../../state/context';

function titleForStep(step: AddAccountStep): string {
  if (step === 'stepImport') return 'Import auth.json';
  if (step === 'stepBrowser') return 'Browser sign-in';
  if (step === 'stepDevice') return 'Device code';
  return 'Add account';
}

function countdownLabel(seconds: number): string {
  const mins = Math.floor(seconds / 60);
  const secs = seconds % 60;
  return `Expires in ${mins}:${String(secs).padStart(2, '0')}`;
}

export function AddAccountDialog() {
  const accounts = useAccountsContext();
  if (!accounts.addAccountOpen) return null;

  const step = accounts.addAccountStep;
  const hasBrowserAuthUrl = accounts.browserAuthUrl.trim().length > 0;

  return (
    <dialog open id="addAccountDialog" onClick={(event) => event.currentTarget === event.target && accounts.closeAddAccountDialog()}>
      <header className="dialog-header">
        <h3>{titleForStep(step)}</h3>
        <button className="dialog-close" type="button" aria-label="Close" onClick={accounts.closeAddAccountDialog}>
          &times;
        </button>
      </header>
      <div className="dialog-body">
        {step === 'stepMethod' && (
          <div className="step active">
            <div className="method-list">
              <button className="method-option" type="button" onClick={() => accounts.setAddAccountStep('stepBrowser')}>
                <strong>
                  Browser sign-in <span className="method-tag">recommended</span>
                </strong>
                <span>Opens a browser window for OpenAI authentication via PKCE</span>
              </button>
              <button
                className="method-option"
                type="button"
                onClick={() => {
                  accounts.setAddAccountStep('stepDevice');
                  accounts.startDeviceFlow();
                }}
              >
                <strong>Device code</strong>
                <span>Enter a code on another device. For headless or remote setups</span>
              </button>
              <button className="method-option" type="button" onClick={() => accounts.setAddAccountStep('stepImport')}>
                <strong>Import auth.json</strong>
                <span>Upload an existing auth.json file with pre-exported credentials</span>
              </button>
            </div>
          </div>
        )}

        {step === 'stepImport' && (
          <div className="step active">
            <label className="file-drop" htmlFor="fileInput">
              <p>Drop auth.json here or click to browse</p>
              <small>JSON file with tokens, lastRefreshAt, and optional OPENAI_API_KEY</small>
              <input
                id="fileInput"
                type="file"
                accept=".json,application/json"
                onChange={(event) => {
                  const file = event.target.files?.[0];
                  if (file) accounts.selectImportFile(file);
                }}
              />
              <div className="file-name">{accounts.selectedFileName}</div>
            </label>
            <div className="dialog-actions">
              <button className="dock-btn" type="button" onClick={() => accounts.setAddAccountStep('stepMethod')}>
                Back
              </button>
              <button className="dock-btn accent" type="button" disabled={!accounts.selectedFileName} onClick={accounts.submitImport}>
                Import
              </button>
            </div>
          </div>
        )}

        {step === 'stepBrowser' && (
          <div className="step active">
            <div className="pending-state">
              <p className="pending-status">Waiting for authorization…</p>
              <div className="url-row">
                <span className="url-value">{hasBrowserAuthUrl ? accounts.browserAuthUrl : 'Preparing authorization URL…'}</span>
                <button className="copy-btn" type="button" disabled={!hasBrowserAuthUrl} onClick={() => void accounts.copyBrowserAuthUrl()}>
                  {accounts.copyAuthLabel}
                </button>
              </div>
              <div className="button-row">
                <button className="dock-btn accent" type="button" onClick={accounts.simulateBrowserAuth}>
                  Open sign-in page
                </button>
              </div>
              <div className="manual-section">
                <label>Remote server? Paste the callback URL after sign-in:</label>
                <div className="url-row">
                  <input
                    className="auth-input"
                    type="text"
                    placeholder="http://127.0.0.1:1455/auth/callback?code=..."
                    value={accounts.manualCallback}
                    onChange={(event) => accounts.setManualCallback(event.target.value)}
                  />
                  <button className="dock-btn" type="button" onClick={accounts.submitManualCallback}>
                    Submit
                  </button>
                </div>
              </div>
            </div>
            <div className="dialog-actions">
              <button className="dock-btn" type="button" onClick={() => accounts.setAddAccountStep('stepMethod')}>
                Cancel
              </button>
            </div>
          </div>
        )}

        {step === 'stepDevice' && (
          <div className="step active">
            <div className="pending-state">
              <p className="pending-status">Enter this code at the verification page:</p>
              <div className="code-display">{accounts.deviceUserCode}</div>
              <div className="url-row">
                <span className="url-value">{accounts.deviceVerifyUrl}</span>
                <button className="copy-btn" type="button" onClick={() => void accounts.copyDeviceVerificationUrl()}>
                  {accounts.copyDeviceLabel}
                </button>
              </div>
              <div className="button-row">
                <button
                  className="dock-btn accent"
                  type="button"
                  onClick={() => window.open(accounts.deviceVerifyUrl, '_blank', 'noopener,noreferrer')}
                >
                  Open verification page
                </button>
              </div>
              <p className="countdown">{countdownLabel(accounts.deviceCountdownSeconds)}</p>
            </div>
            <div className="dialog-actions">
              <button className="dock-btn" type="button" onClick={accounts.cancelDeviceFlow}>
                Cancel
              </button>
            </div>
          </div>
        )}

        {step === 'stepSuccess' && (
          <div className="step active">
            <div className="success-state">
              <div className="success-check">
                <CheckCircle2 size={20} strokeWidth={2.25} aria-hidden="true" />
              </div>
              <p>Account added</p>
              <small>{accounts.successEmail}</small>
              <small>{accounts.successPlan}</small>
            </div>
            <div className="dialog-actions">
              <button className="dock-btn accent" type="button" onClick={accounts.closeAddAccountDialog}>
                Done
              </button>
            </div>
          </div>
        )}

        {step === 'stepError' && (
          <div className="step active">
            <div className="error-state">
              <p>{accounts.addAccountError}</p>
            </div>
            <div className="dialog-actions">
              <button className="dock-btn" type="button" onClick={() => accounts.setAddAccountStep('stepMethod')}>
                Try again
              </button>
              <button className="dock-btn" type="button" onClick={accounts.closeAddAccountDialog}>
                Close
              </button>
            </div>
          </div>
        )}
      </div>
    </dialog>
  );
}
