import { useEffect, useRef } from 'react';
import { reportWarn } from '../errors';
import type { StatusNoticeLevel } from '../statusNotices';

export interface UseBootstrapOptions {
  refreshAccountsFromNative: () => Promise<unknown>;
  refreshBackendState: () => Promise<void>;
  refreshAccountTrafficSnapshot: () => Promise<void>;
  refreshStickySessions: () => Promise<void>;
  refreshRequestLogs: () => Promise<void>;
  refreshDashboardSettingsFromNative: () => Promise<void>;
  refreshFirewallIps: () => Promise<void>;
  refreshClusterState: () => Promise<void>;
  bootstrapOauthState: () => Promise<void>;
  pushRuntimeEvent: (text: string, level?: StatusNoticeLevel) => void;
}

export function useBootstrap(options: UseBootstrapOptions): void {
  const optionsRef = useRef(options);
  optionsRef.current = options;

  useEffect(() => {
    let cancelled = false;

    async function bootstrap(): Promise<void> {
      const current = optionsRef.current;
      try {
        await current.refreshAccountsFromNative();
      } catch (error) {
        if (!cancelled) {
          reportWarn(current.pushRuntimeEvent, error, 'Failed to load accounts');
        }
      }

      try {
        await current.refreshBackendState();
      } catch (error) {
        if (!cancelled) {
          reportWarn(current.pushRuntimeEvent, error, 'Failed to load backend state');
        }
      }

      try {
        await current.refreshAccountTrafficSnapshot();
      } catch {
        // websocket stream will continue to deliver updates
      }

      try {
        await current.refreshStickySessions();
      } catch (error) {
        if (!cancelled) {
          reportWarn(current.pushRuntimeEvent, error, 'Failed to load sticky sessions');
        }
      }

      try {
        await current.refreshRequestLogs();
      } catch (error) {
        if (!cancelled) {
          reportWarn(current.pushRuntimeEvent, error, 'Failed to load request logs');
        }
      }

      try {
        await current.refreshDashboardSettingsFromNative();
      } catch (error) {
        if (!cancelled) {
          reportWarn(current.pushRuntimeEvent, error, 'Failed to load settings');
        }
      }

      try {
        await current.refreshFirewallIps();
      } catch (error) {
        if (!cancelled) {
          reportWarn(current.pushRuntimeEvent, error, 'Failed to load firewall allowlist');
        }
      }

      try {
        await current.refreshClusterState();
      } catch {
        // cluster can be disabled
      }

      try {
        await current.bootstrapOauthState();
      } catch (error) {
        if (!cancelled) {
          reportWarn(current.pushRuntimeEvent, error, 'Failed to initialize OAuth state');
        }
      }
    }

    void bootstrap();

    return () => {
      cancelled = true;
    };
  }, []);
}
