import { useEffect, useRef, useState } from 'react';
import { useTightropeService } from './context';
import type { ClusterStatus, SyncEvent } from '../shared/types';

const TOPOLOGY_REFRESH_MS = 2000;

export interface SyncTopologyState {
  status: ClusterStatus | null;
  lastEventAt: number | null;
}

export function useSyncTopology(open: boolean): SyncTopologyState {
  const service = useTightropeService();
  const [status, setStatus] = useState<ClusterStatus | null>(null);
  const [lastEventAt, setLastEventAt] = useState<number | null>(null);
  const unsubscribeRef = useRef<(() => void) | null>(null);

  useEffect(() => {
    if (!open) {
      if (unsubscribeRef.current) {
        unsubscribeRef.current();
        unsubscribeRef.current = null;
      }
      return;
    }

    let active = true;
    const refreshStatus = () => {
      void service.getClusterStatusRequest().then((next) => {
        if (!active || !next) return;
        setStatus(next);
      });
    };

    // Seed initial state and reconcile membership/liveness periodically while open.
    refreshStatus();
    const interval = window.setInterval(refreshStatus, TOPOLOGY_REFRESH_MS);

    // Subscribe to live events
    const unsubscribe = service.onSyncEventRequest((event: SyncEvent) => {
      setLastEventAt(event.ts);
      setStatus((prev) => {
        if (!prev) return prev;
        return applyEvent(prev, event);
      });
    });
    if (!unsubscribe) {
      window.clearInterval(interval);
      return;
    }
    unsubscribeRef.current = unsubscribe;

    return () => {
      active = false;
      window.clearInterval(interval);
      unsubscribe();
      unsubscribeRef.current = null;
    };
  }, [open, service]);

  return { status, lastEventAt };
}

function applyEvent(prev: ClusterStatus, event: SyncEvent): ClusterStatus {
  switch (event.type) {
    case 'peer_state_change': {
      const peers = prev.peers.map((p) =>
        p.site_id === event.site_id ? { ...p, state: event.state } : p,
      );
      return { ...prev, peers };
    }
    case 'commit_advance': {
      return { ...prev, commit_index: event.commit_index };
    }
    case 'role_change': {
      return {
        ...prev,
        role: event.role,
        term: event.term,
        leader_id: event.leader_id,
      };
    }
    case 'term_change': {
      return { ...prev, term: event.term };
    }
    case 'journal_entry': {
      return { ...prev, journal_entries: prev.journal_entries + 1 };
    }
    case 'ingress_batch': {
      const peers = prev.peers.map((p) => {
        if (p.site_id !== event.site_id) return p;
        return {
          ...p,
          ingress_last_apply_duration_ms: event.apply_duration_ms,
          ingress_last_replication_latency_ms: event.replication_latency_ms,
          ingress_last_wire_batch_bytes: event.bytes,
          ingress_accepted_batches: event.accepted
            ? p.ingress_accepted_batches + 1
            : p.ingress_accepted_batches,
          ingress_rejected_batches: !event.accepted
            ? p.ingress_rejected_batches + 1
            : p.ingress_rejected_batches,
        };
      });
      return { ...prev, peers };
    }
    case 'lag_alert': {
      return {
        ...prev,
        replication_lag_alert_active: event.active,
        replication_lagging_peers: event.lagging_peers,
        replication_lag_max_entries: event.max_lag,
      };
    }
    default:
      return prev;
  }
}
