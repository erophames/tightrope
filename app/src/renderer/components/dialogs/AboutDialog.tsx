import type { AppMetaResponse } from '../../shared/types';
import loadBalancerIcon from '../../assets/load_balancer.svg';

interface AboutDialogProps {
  open: boolean;
  platform: NodeJS.Platform | null;
  appMeta: AppMetaResponse | null;
  onClose: () => void;
}

function platformLabel(platform: NodeJS.Platform | null): string {
  if (platform === 'darwin') {
    return 'macOS';
  }
  if (platform === 'win32') {
    return 'Windows';
  }
  if (platform === 'linux') {
    return 'Linux';
  }
  return 'Desktop';
}

export function AboutDialog({ open, platform, appMeta, onClose }: AboutDialogProps) {
  if (!open) {
    return null;
  }

  const year = new Date().getFullYear();
  const buildLabel = appMeta ? `${appMeta.version} (${appMeta.buildChannel})` : 'Unavailable';

  return (
    <dialog open id="aboutDialog" onClick={(event) => event.currentTarget === event.target && onClose()}>
      <header className="dialog-header about-header">
        <div className="about-header-copy">
          <span className="about-kicker">About</span>
          <h3>tightrope routing workbench</h3>
        </div>
        <button className="dialog-close" type="button" aria-label="Close" onClick={onClose}>
          &times;
        </button>
      </header>
      <div className="dialog-body about-body">
        <section className="about-hero">
          <div className="about-mark-wrap" aria-hidden="true">
            <img className="about-mark" src={loadBalancerIcon} alt="" />
          </div>
          <div className="about-copy">
            <span>Live account routing and telemetry for desktop operations.</span>
          </div>
        </section>
        <div className="about-meta-grid">
          <div>
            <span>Platform</span>
            <strong>{platformLabel(platform)}</strong>
          </div>
          <div>
            <span>Runtime</span>
            <strong>Desktop</strong>
          </div>
          <div>
            <span>Copyright</span>
            <strong>{`© ${year} tightrope`}</strong>
          </div>
          <div>
            <span>Build</span>
            <strong>{buildLabel}</strong>
          </div>
        </div>
      </div>
    </dialog>
  );
}
