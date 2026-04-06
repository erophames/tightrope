import { useEffect, useState } from 'react';
import { useTightropeService } from '../../state/context';

type WindowControlAction = 'close' | 'minimize' | 'maximize';

interface TitleBarProps {
  onOpenAbout?: () => void;
}

export function TitleBar({ onOpenAbout }: TitleBarProps) {
  const service = useTightropeService();
  const [isMac, setIsMac] = useState(false);
  const [isMaximized, setIsMaximized] = useState(false);

  useEffect(() => {
    const detectedPlatform =
      service.platformRequest() ??
      (navigator.platform.toLowerCase().includes('mac') ? 'darwin' : 'unknown');
    const mac = detectedPlatform === 'darwin';
    setIsMac(mac);
    if (!mac) return;

    void service.windowIsMaximizedRequest()
      .then((maximized) => {
        setIsMaximized(maximized ?? false);
      })
      .catch(() => {
        setIsMaximized(false);
      });
  }, [service]);

  function onWindowControl(action: WindowControlAction): void {
    if (action === 'close') {
      void service.windowCloseRequest();
      return;
    }

    if (action === 'minimize') {
      void service.windowMinimizeRequest();
      return;
    }

    void service.windowToggleMaximizeRequest()
      .then((toggled) => {
        if (!toggled) {
          return null;
        }
        return service.windowIsMaximizedRequest();
      })
      .then((maximized) => {
        if (typeof maximized === 'boolean') {
          setIsMaximized(maximized);
        }
      })
      .catch(() => {
        setIsMaximized((previous) => !previous);
      });
  }

  return (
    <header className="titlebar" aria-label="Window title">
      {isMac && (
        <div className="titlebar-controls" aria-label="Window controls">
          <button
            className="titlebar-control close"
            type="button"
            aria-label="Close window"
            onClick={() => onWindowControl('close')}
          />
          <button
            className="titlebar-control minimize"
            type="button"
            aria-label="Minimize window"
            onClick={() => onWindowControl('minimize')}
          />
          <button
            className={`titlebar-control maximize${isMaximized ? ' active' : ''}`}
            type="button"
            aria-label={isMaximized ? 'Restore window' : 'Maximize window'}
            onClick={() => onWindowControl('maximize')}
          />
        </div>
      )}

      <div className="titlecopy">
        <strong>tightrope</strong>
        <span>routing workbench</span>
      </div>
      <div className="titlebar-spacer" />
      {!isMac && onOpenAbout ? (
        <button className="titlebar-action" type="button" aria-label="About tightrope" onClick={onOpenAbout}>
          ⓘ
        </button>
      ) : null}
    </header>
  );
}
