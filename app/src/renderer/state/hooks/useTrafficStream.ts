import { useEffect, useRef, useState } from 'react';

const TRAFFIC_WS_ENDPOINT = '/api/accounts/traffic/ws';
const TRAFFIC_WS_HOST = '127.0.0.1:2455';
const TRAFFIC_FRAME_VERSION = 1;
const TRAFFIC_FRAME_BYTES = 50;
const TRAFFIC_WS_BIND_PATTERN = /^([A-Za-z0-9._-]+):(\d+)$/;

export interface AccountTrafficFrame {
  accountId: string;
  upBytes: number;
  downBytes: number;
  lastUpAtMs: number;
  lastDownAtMs: number;
}

interface UseTrafficStreamOptions {
  runtimeBind: string;
  enableWebSocket: boolean;
  trafficReconnectMs: number;
  trafficClockTickMs: number;
  trafficSnapshotPollMs: number;
  trafficActiveWindowMs: number;
  onTrafficFrame: (frame: AccountTrafficFrame) => void;
  refreshSnapshot: () => Promise<void>;
  reportPollingError?: (message: string) => void;
}

interface UseTrafficStreamResult {
  trafficClockMs: number;
  trafficActiveWindowMs: number;
}

function trafficWsUrl(bindLabel: string | null | undefined): string {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const trimmedBind = (bindLabel ?? '').trim();
  const host = normalizeTrafficWsHost(trimmedBind);
  return `${protocol}//${host}${TRAFFIC_WS_ENDPOINT}`;
}

function normalizeTrafficWsHost(trimmedBind: string): string {
  const match = TRAFFIC_WS_BIND_PATTERN.exec(trimmedBind);
  if (!match) {
    return TRAFFIC_WS_HOST;
  }

  const host = match[1].toLowerCase();
  const port = match[2];
  if (host === '0.0.0.0' || host === '::' || host === '[::]' || host === 'localhost') {
    return `127.0.0.1:${port}`;
  }

  return `${match[1]}:${port}`;
}

function bigintToNumber(value: bigint): number {
  const max = BigInt(Number.MAX_SAFE_INTEGER);
  return Number(value > max ? max : value);
}

function decodeTrafficFrame(data: ArrayBuffer): AccountTrafficFrame | null {
  if (data.byteLength !== TRAFFIC_FRAME_BYTES) {
    return null;
  }

  const view = new DataView(data);
  const version = view.getUint8(0);
  if (version !== TRAFFIC_FRAME_VERSION) {
    return null;
  }

  const accountId = bigintToNumber(view.getBigUint64(10, true)).toString();
  if (accountId.length === 0) {
    return null;
  }

  return {
    accountId,
    upBytes: bigintToNumber(view.getBigUint64(18, true)),
    downBytes: bigintToNumber(view.getBigUint64(26, true)),
    lastUpAtMs: bigintToNumber(view.getBigUint64(34, true)),
    lastDownAtMs: bigintToNumber(view.getBigUint64(42, true)),
  };
}

export function useTrafficStream(options: UseTrafficStreamOptions): UseTrafficStreamResult {
  const {
    runtimeBind,
    enableWebSocket,
    trafficReconnectMs,
    trafficClockTickMs,
    trafficSnapshotPollMs,
    trafficActiveWindowMs,
    onTrafficFrame,
    refreshSnapshot,
    reportPollingError,
  } = options;
  const [trafficClockMs, setTrafficClockMs] = useState<number>(() => Date.now());
  const trafficWsRef = useRef<WebSocket | null>(null);
  const trafficReconnectTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const trafficClockTimerRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const snapshotPollErrorReportedRef = useRef(false);
  const wsSnapshotErrorReportedRef = useRef(false);

  useEffect(() => {
    if (trafficClockTimerRef.current) {
      clearInterval(trafficClockTimerRef.current);
    }
    trafficClockTimerRef.current = setInterval(() => {
      setTrafficClockMs(Date.now());
    }, trafficClockTickMs);

    return () => {
      if (trafficClockTimerRef.current) {
        clearInterval(trafficClockTimerRef.current);
        trafficClockTimerRef.current = null;
      }
    };
  }, [trafficClockTickMs]);

  useEffect(() => {
    const handle = setInterval(() => {
      void refreshSnapshot()
        .then(() => {
          snapshotPollErrorReportedRef.current = false;
        })
        .catch(() => {
          if (snapshotPollErrorReportedRef.current) {
            return;
          }
          snapshotPollErrorReportedRef.current = true;
          reportPollingError?.('traffic snapshot polling failed; retrying');
        });
    }, trafficSnapshotPollMs);

    return () => {
      clearInterval(handle);
    };
  }, [refreshSnapshot, reportPollingError, trafficSnapshotPollMs]);

  useEffect(() => {
    if (!enableWebSocket || typeof WebSocket === 'undefined') {
      return;
    }

    let disposed = false;

    const clearReconnectTimer = () => {
      if (trafficReconnectTimerRef.current) {
        clearTimeout(trafficReconnectTimerRef.current);
        trafficReconnectTimerRef.current = null;
      }
    };

    const scheduleReconnect = () => {
      if (disposed) {
        return;
      }
      clearReconnectTimer();
      trafficReconnectTimerRef.current = setTimeout(() => {
        connect();
      }, trafficReconnectMs);
    };

    const applyFrameFromBuffer = (buffer: ArrayBuffer) => {
      const frame = decodeTrafficFrame(buffer);
      if (!frame) {
        return;
      }
      onTrafficFrame(frame);
    };

    const connect = () => {
      if (disposed) {
        return;
      }

      let ws: WebSocket;
      try {
        ws = new WebSocket(trafficWsUrl(runtimeBind));
      } catch {
        scheduleReconnect();
        return;
      }

      ws.binaryType = 'arraybuffer';
      trafficWsRef.current = ws;

      ws.onopen = () => {
        if (disposed) {
          return;
        }
        ws.send('snapshot');
        void refreshSnapshot()
          .then(() => {
            wsSnapshotErrorReportedRef.current = false;
          })
          .catch(() => {
            if (wsSnapshotErrorReportedRef.current) {
              return;
            }
            wsSnapshotErrorReportedRef.current = true;
            reportPollingError?.('traffic websocket snapshot refresh failed; retrying');
          });
      };

      ws.onmessage = (event: MessageEvent) => {
        if (disposed) {
          return;
        }
        const data = event.data;
        if (data instanceof ArrayBuffer) {
          applyFrameFromBuffer(data);
          return;
        }
        if (data instanceof Blob) {
          void data.arrayBuffer().then((buffer) => {
            if (!disposed) {
              applyFrameFromBuffer(buffer);
            }
          });
        }
      };

      ws.onclose = () => {
        if (trafficWsRef.current === ws) {
          trafficWsRef.current = null;
        }
        scheduleReconnect();
      };

      ws.onerror = () => {
        ws.close();
      };
    };

    connect();

    return () => {
      disposed = true;
      clearReconnectTimer();
      if (trafficWsRef.current) {
        trafficWsRef.current.close();
        trafficWsRef.current = null;
      }
    };
  }, [enableWebSocket, onTrafficFrame, refreshSnapshot, runtimeBind, trafficReconnectMs]);

  return {
    trafficClockMs,
    trafficActiveWindowMs,
  };
}
