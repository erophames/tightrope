import { useEffect, useMemo, useRef, useState, type CSSProperties } from 'react';
import type { Account, RouteMetrics } from '../../../shared/types';
import { RoutingPoolSortSelect, type RoutingPoolSortOption } from './RoutingPoolSortSelect';

interface RouterPoolPaneProps {
  accounts: Account[];
  metrics: Map<string, RouteMetrics>;
  routedAccountId: string | null;
  lockedRoutingAccountIds: string[];
  recentRouteActivityByAccount: Map<string, number>;
  trafficNowMs: number;
  trafficActiveWindowMs: number;
  selectedAccountId: string;
  onSelectAccount: (accountId: string) => void;
  onTogglePin: (accountId: string, nextPinned: boolean) => void;
  onUpdateLockedRoutingAccountIds: (accountIds: string[]) => Promise<boolean>;
  onOpenAddAccount: () => void;
}

interface LockConnectorPoint {
  x: number;
  y: number;
}

interface LockConnectorGeometry {
  points: LockConnectorPoint[];
  minY: number;
  maxY: number;
  railX: number;
  canvasWidth: number;
  canvasHeight: number;
}

type RoutingPoolSortKey =
  | 'reset_soon'
  | 'remaining_desc'
  | 'remaining_asc'
  | 'name_asc'
  | 'plan';

const ROUTING_POOL_SORT_OPTIONS: ReadonlyArray<RoutingPoolSortOption & { key: RoutingPoolSortKey }> = [
  {
    key: 'reset_soon',
    label: 'Resetting soon',
    description: 'Free uses weekly reset, paid uses hourly/5-hour reset.',
  },
  {
    key: 'remaining_desc',
    label: 'Most quota left',
    description: 'Show highest remaining plan-relevant quota first.',
  },
  {
    key: 'remaining_asc',
    label: 'Least quota left',
    description: 'Surface accounts nearest exhaustion.',
  },
  {
    key: 'name_asc',
    label: 'Name A-Z',
    description: 'Alphabetical account name order.',
  },
  {
    key: 'plan',
    label: 'Plan tier',
    description: 'Enterprise, Plus, then Free.',
  },
];

function clampPercent(value: number): number {
  if (!Number.isFinite(value)) {
    return 0;
  }
  return Math.min(100, Math.max(0, Math.round(value)));
}

function primaryQuotaWindowLabel(account: Account): string {
  if (account.plan === 'free') {
    return 'Weekly';
  }
  const windowSeconds = account.quotaPrimaryWindowSeconds;
  if (typeof windowSeconds === 'number' && Number.isFinite(windowSeconds) && windowSeconds > 0) {
    if (windowSeconds <= 6 * 60 * 60) {
      return '5-hour';
    }
    if (windowSeconds >= 6 * 24 * 60 * 60) {
      return 'Weekly';
    }
    if (windowSeconds >= 20 * 60 * 60 && windowSeconds <= 28 * 60 * 60) {
      return 'Daily';
    }
    const roundedHours = Math.round(windowSeconds / (60 * 60));
    if (roundedHours >= 1 && roundedHours <= 23) {
      return `${roundedHours}-hour`;
    }
    const roundedDays = Math.round(windowSeconds / (24 * 60 * 60));
    if (roundedDays >= 2) {
      return `${roundedDays}-day`;
    }
  }
  return '5-hour';
}

function hasPositiveFiniteNumber(value: number | null | undefined): value is number {
  return typeof value === 'number' && Number.isFinite(value) && value > 0;
}

function usesSecondaryAsPlanPrimary(account: Account): boolean {
  return (
    account.plan === 'free' &&
    (account.hasSecondaryQuota === true ||
      hasPositiveFiniteNumber(account.quotaSecondaryWindowSeconds) ||
      hasPositiveFiniteNumber(account.quotaSecondaryResetAtMs))
  );
}

function planPrimaryUsagePercent(account: Account): number | null {
  if (!account.telemetryBacked) {
    return null;
  }
  if (usesSecondaryAsPlanPrimary(account)) {
    return clampPercent(account.quotaSecondary);
  }
  return clampPercent(account.quotaPrimary);
}

function accountResetAtMsForSort(account: Account): number | null {
  const primaryReset = hasPositiveFiniteNumber(account.quotaPrimaryResetAtMs) ? account.quotaPrimaryResetAtMs : null;
  const secondaryReset = hasPositiveFiniteNumber(account.quotaSecondaryResetAtMs) ? account.quotaSecondaryResetAtMs : null;
  if (usesSecondaryAsPlanPrimary(account)) {
    return secondaryReset ?? primaryReset;
  }
  return primaryReset ?? secondaryReset;
}

function formatResetCountdown(resetAtMs: number | null, nowMs: number): string | null {
  if (resetAtMs === null || !Number.isFinite(resetAtMs) || resetAtMs <= 0) {
    return null;
  }
  const remainingMs = Math.max(0, resetAtMs - nowMs);
  const totalMinutes = Math.ceil(remainingMs / (60 * 1000));
  if (totalMinutes <= 0) {
    return 'now';
  }
  const days = Math.floor(totalMinutes / (24 * 60));
  const hours = Math.floor((totalMinutes % (24 * 60)) / 60);
  const minutes = totalMinutes % 60;
  if (days > 0) {
    if (hours > 0) {
      return `${days}d${hours}h${minutes}m`;
    }
    return `${days}d${minutes}m`;
  }
  if (hours > 0) {
    return `${hours}h${minutes}m`;
  }
  return `${minutes}m`;
}

function planRelevantRemainingPercent(account: Account): number | null {
  const usage = planPrimaryUsagePercent(account);
  return usage === null ? null : Math.max(0, 100 - usage);
}

function planSortWeight(plan: Account['plan']): number {
  if (plan === 'enterprise') {
    return 0;
  }
  if (plan === 'plus') {
    return 1;
  }
  return 2;
}

function compareNullableNumbers(left: number | null, right: number | null, direction: 'asc' | 'desc'): number {
  if (left === null && right === null) {
    return 0;
  }
  if (left === null) {
    return 1;
  }
  if (right === null) {
    return -1;
  }
  return direction === 'asc' ? left - right : right - left;
}

function compareAccountsBySortKey(
  key: RoutingPoolSortKey,
  left: Account,
  right: Account,
  nowMs: number,
): number {
  if (key === 'name_asc') {
    return left.name.localeCompare(right.name, undefined, { sensitivity: 'base' });
  }
  if (key === 'plan') {
    return planSortWeight(left.plan) - planSortWeight(right.plan);
  }
  if (key === 'remaining_desc') {
    return compareNullableNumbers(planRelevantRemainingPercent(left), planRelevantRemainingPercent(right), 'desc');
  }
  if (key === 'remaining_asc') {
    return compareNullableNumbers(planRelevantRemainingPercent(left), planRelevantRemainingPercent(right), 'asc');
  }

  const leftResetAt = accountResetAtMsForSort(left);
  const rightResetAt = accountResetAtMsForSort(right);
  const leftRemainingMs = leftResetAt === null ? null : Math.max(0, leftResetAt - nowMs);
  const rightRemainingMs = rightResetAt === null ? null : Math.max(0, rightResetAt - nowMs);
  return compareNullableNumbers(leftRemainingMs, rightRemainingMs, 'asc');
}

function activityDecayStrength(lastAtMs: number | null | undefined, nowMs: number, activeWindowMs: number): number {
  if (typeof lastAtMs !== 'number' || !Number.isFinite(lastAtMs) || lastAtMs <= 0 || activeWindowMs <= 0) {
    return 0;
  }
  const ageMs = nowMs - lastAtMs;
  if (!Number.isFinite(ageMs) || ageMs < 0 || ageMs > activeWindowMs) {
    return 0;
  }
  return Math.max(0, 1 - ageMs / activeWindowMs);
}

export function RouterPoolPane({
  accounts,
  metrics,
  routedAccountId,
  lockedRoutingAccountIds,
  recentRouteActivityByAccount,
  trafficNowMs,
  trafficActiveWindowMs,
  selectedAccountId,
  onSelectAccount,
  onTogglePin,
  onUpdateLockedRoutingAccountIds,
  onOpenAddAccount,
}: RouterPoolPaneProps) {
  const paneRef = useRef<HTMLElement | null>(null);
  const accountsListRef = useRef<HTMLDivElement | null>(null);
  const lockButtonRefs = useRef<Map<string, HTMLButtonElement>>(new Map());
  const previousLockGroupRoutingActiveRef = useRef(false);
  const previousLockGroupCountRef = useRef(0);
  const [openTooltipAccountId, setOpenTooltipAccountId] = useState<string | null>(null);
  const [lockGroupAccountIds, setLockGroupAccountIds] = useState<string[]>(() =>
    Array.isArray(lockedRoutingAccountIds)
      ? Array.from(new Set(lockedRoutingAccountIds.map((value) => value.trim()).filter((value) => value.length > 0)))
      : [],
  );
  const [lockConnector, setLockConnector] = useState<LockConnectorGeometry | null>(null);
  const [lockGroupChargeActive, setLockGroupChargeActive] = useState(false);
  const [sortKeys, setSortKeys] = useState<RoutingPoolSortKey[]>(['reset_soon']);
  const recentRouteActiveWindowMs = Math.max(trafficActiveWindowMs, 30_000);

  const primarySortKey = sortKeys[0] ?? 'reset_soon';
  const secondarySortKey = sortKeys[1] ?? null;
  const secondarySortOptions = useMemo(
    () => ROUTING_POOL_SORT_OPTIONS.filter((option) => option.key !== primarySortKey),
    [primarySortKey],
  );
  const accountsById = useMemo(() => new Map(accounts.map((account) => [account.id, account])), [accounts]);
  const lockGroupOrder = useMemo(() => {
    const order = new Map<string, number>();
    lockGroupAccountIds.forEach((accountId, index) => {
      if (accountsById.has(accountId)) {
        order.set(accountId, index);
      }
    });
    return order;
  }, [accountsById, lockGroupAccountIds]);
  const lockGroupSize = lockGroupOrder.size;
  const [lockGroupTopBlockActive, setLockGroupTopBlockActive] = useState(() => lockGroupSize >= 2);
  const lockGroupSet = useMemo(() => new Set(lockGroupAccountIds), [lockGroupAccountIds]);
  const pinnedAccountIds = useMemo(
    () => accounts.filter((account) => account.pinned).map((account) => account.id),
    [accounts],
  );
  const pinnedTopSet = useMemo(() => new Set(pinnedAccountIds), [pinnedAccountIds]);
  const anchoredTopSet = useMemo(() => {
    const anchored = new Set<string>();
    if (lockGroupTopBlockActive) {
      for (const accountId of lockGroupOrder.keys()) {
        anchored.add(accountId);
      }
    }
    for (const accountId of pinnedTopSet) {
      anchored.add(accountId);
    }
    return anchored;
  }, [lockGroupOrder, lockGroupTopBlockActive, pinnedTopSet]);
  const canAddSecondarySort = secondarySortKey === null && secondarySortOptions.length > 0;
  const sortedAccounts = useMemo(() => {
    const next = [...accounts];
    next.sort((left, right) => {
      const leftAnchored = anchoredTopSet.has(left.id);
      const rightAnchored = anchoredTopSet.has(right.id);
      if (leftAnchored !== rightAnchored) {
        return leftAnchored ? -1 : 1;
      }

      if (lockGroupTopBlockActive) {
        const leftLockIndex = lockGroupOrder.get(left.id);
        const rightLockIndex = lockGroupOrder.get(right.id);
        const leftInLockGroup = leftLockIndex !== undefined;
        const rightInLockGroup = rightLockIndex !== undefined;
        if (leftInLockGroup !== rightInLockGroup) {
          return leftInLockGroup ? -1 : 1;
        }
        if (leftInLockGroup && rightInLockGroup) {
          return leftLockIndex - rightLockIndex;
        }
      }

      for (const key of sortKeys) {
        const delta = compareAccountsBySortKey(key, left, right, trafficNowMs);
        if (delta !== 0) {
          return delta;
        }
      }
      return left.name.localeCompare(right.name, undefined, { sensitivity: 'base' });
    });
    return next;
  }, [accounts, anchoredTopSet, lockGroupOrder, lockGroupTopBlockActive, sortKeys, trafficNowMs]);
  const sortedAccountIdsKey = useMemo(() => sortedAccounts.map((account) => account.id).join(','), [sortedAccounts]);

  useEffect(() => {
    const previousCount = previousLockGroupCountRef.current;
    previousLockGroupCountRef.current = lockGroupSize;
    if (lockGroupSize < 2) {
      setLockGroupTopBlockActive(false);
      return;
    }
    if (previousCount < 2) {
      setLockGroupTopBlockActive(false);
      const timer = window.setTimeout(() => {
        setLockGroupTopBlockActive(true);
      }, 5_000);
      return () => {
        window.clearTimeout(timer);
      };
    }
    setLockGroupTopBlockActive(true);
  }, [lockGroupSize]);

  useEffect(() => {
    if (!openTooltipAccountId) {
      return;
    }
    const handlePointerDown = (event: MouseEvent) => {
      const target = event.target;
      if (!(target instanceof Element)) {
        return;
      }
      const inPane = paneRef.current?.contains(target) ?? false;
      if (inPane && (target.closest('.account-info-btn') || target.closest('.account-stats-tooltip'))) {
        return;
      }
      setOpenTooltipAccountId(null);
    };
    const handleEscape = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        setOpenTooltipAccountId(null);
      }
    };
    window.addEventListener('mousedown', handlePointerDown);
    window.addEventListener('keydown', handleEscape);
    return () => {
      window.removeEventListener('mousedown', handlePointerDown);
      window.removeEventListener('keydown', handleEscape);
    };
  }, [openTooltipAccountId]);

  useEffect(() => {
    const activeIds = new Set(accounts.map((account) => account.id));
    setLockGroupAccountIds((previous) => {
      const next = previous.filter((accountId) => activeIds.has(accountId));
      if (next.length === previous.length) {
        return previous;
      }
      void onUpdateLockedRoutingAccountIds(next);
      return next;
    });
  }, [accounts, onUpdateLockedRoutingAccountIds]);

  useEffect(() => {
    const next = Array.isArray(lockedRoutingAccountIds)
      ? Array.from(new Set(lockedRoutingAccountIds.map((value) => value.trim()).filter((value) => value.length > 0)))
      : [];
    setLockGroupAccountIds((previous) => {
      if (previous.length === next.length && previous.every((value, index) => value === next[index])) {
        return previous;
      }
      return next;
    });
  }, [lockedRoutingAccountIds]);

  useEffect(() => {
    if (lockGroupAccountIds.length < 2) {
      setLockConnector(null);
      return;
    }

    const computeConnector = () => {
      const listElement = accountsListRef.current;
      if (!listElement) {
        setLockConnector(null);
        return;
      }
      const listRect = listElement.getBoundingClientRect();
      const scrollTop = listElement.scrollTop;
      const scrollLeft = listElement.scrollLeft;

      const points: LockConnectorPoint[] = [];
      let maxCardEdgeX = 0;
      for (const accountId of lockGroupAccountIds) {
        const button = lockButtonRefs.current.get(accountId);
        if (!button) {
          continue;
        }
        const buttonRect = button.getBoundingClientRect();
        points.push({
          x: buttonRect.right - listRect.left + scrollLeft,
          y: buttonRect.top + buttonRect.height / 2 - listRect.top + scrollTop,
        });
        const card = button.closest('.account-item');
        if (card instanceof HTMLElement) {
          const cardRect = card.getBoundingClientRect();
          const cardRight = cardRect.right - listRect.left + scrollLeft;
          if (cardRight > maxCardEdgeX) {
            maxCardEdgeX = cardRight;
          }
        }
      }

      if (points.length < 2) {
        setLockConnector(null);
        return;
      }

      points.sort((left, right) => left.y - right.y);
      const minY = points[0]?.y ?? 0;
      const maxY = points[points.length - 1]?.y ?? 0;
      const maxPointX = points.reduce((current, point) => Math.max(current, point.x), 0);
      const paneWidth = listElement.clientWidth;
      // Keep rail outside the card edge so the connector never sits on top of card content.
      const outsideCardRailX = maxCardEdgeX > 0 ? maxCardEdgeX + 6 : paneWidth + 2;
      const railX = Math.max(maxPointX + 4, outsideCardRailX);
      const canvasWidth = Math.max(paneWidth, railX + 6);
      const canvasHeight = Math.max(listElement.scrollHeight, maxY + 8);

      setLockConnector({
        points,
        minY,
        maxY,
        railX,
        canvasWidth,
        canvasHeight,
      });
    };

    const scrollTargets: EventTarget[] = [window];
    const listElement = accountsListRef.current;
    if (listElement) {
      scrollTargets.push(listElement);
      let parent: HTMLElement | null = listElement.parentElement;
      while (parent) {
        const style = window.getComputedStyle(parent);
        const overflowValue = `${style.overflow}${style.overflowX}${style.overflowY}`;
        if (/(auto|scroll|overlay)/.test(overflowValue)) {
          scrollTargets.push(parent);
        }
        parent = parent.parentElement;
      }
    }

    let frame: number | null = null;
    const scheduleCompute = () => {
      if (frame !== null) {
        window.cancelAnimationFrame(frame);
      }
      frame = window.requestAnimationFrame(() => {
        frame = null;
        computeConnector();
      });
    };

    let resizeObserver: ResizeObserver | null = null;
    let mutationObserver: MutationObserver | null = null;

    if (typeof ResizeObserver !== 'undefined' && listElement) {
      resizeObserver = new ResizeObserver(() => {
        scheduleCompute();
      });
      resizeObserver.observe(listElement);
      for (const accountId of lockGroupAccountIds) {
        const button = lockButtonRefs.current.get(accountId);
        if (button) {
          resizeObserver.observe(button);
        }
      }
    }

    if (typeof MutationObserver !== 'undefined' && listElement) {
      mutationObserver = new MutationObserver(() => {
        scheduleCompute();
      });
      mutationObserver.observe(listElement, {
        childList: true,
        subtree: true,
        characterData: true,
        attributes: true,
      });
    }

    scheduleCompute();
    window.addEventListener('resize', scheduleCompute);
    for (const target of scrollTargets) {
      target.addEventListener('scroll', scheduleCompute, { passive: true });
    }
    return () => {
      if (frame !== null) {
        window.cancelAnimationFrame(frame);
      }
      resizeObserver?.disconnect();
      mutationObserver?.disconnect();
      window.removeEventListener('resize', scheduleCompute);
      for (const target of scrollTargets) {
        target.removeEventListener('scroll', scheduleCompute);
      }
    };
  }, [lockGroupAccountIds, openTooltipAccountId, sortedAccountIdsKey]);

  function isLockGroupedAccount(accountId: string): boolean {
    return lockGroupAccountIds.includes(accountId);
  }

  function lockButtonRef(accountId: string) {
    return (element: HTMLButtonElement | null) => {
      if (!element) {
        lockButtonRefs.current.delete(accountId);
        return;
      }
      lockButtonRefs.current.set(accountId, element);
    };
  }

  const lockConnectorPath =
    lockConnector === null
      ? null
      : [
          ...lockConnector.points.map((point) => `M ${point.x} ${point.y} L ${lockConnector.railX} ${point.y}`),
          `M ${lockConnector.railX} ${lockConnector.minY} L ${lockConnector.railX} ${lockConnector.maxY}`,
        ].join(' ');
  const lockConnectorFlowPath =
    lockConnector === null || lockConnector.points.length < 2
      ? null
      : `M ${lockConnector.points[0]!.x} ${lockConnector.points[0]!.y} ` +
          `L ${lockConnector.railX} ${lockConnector.points[0]!.y} ` +
          `L ${lockConnector.railX} ${lockConnector.points[lockConnector.points.length - 1]!.y} ` +
          `L ${lockConnector.points[lockConnector.points.length - 1]!.x} ${lockConnector.points[lockConnector.points.length - 1]!.y}`;
  const lockConnectorFlowPathReverse =
    lockConnector === null || lockConnector.points.length < 2
      ? null
      : `M ${lockConnector.points[lockConnector.points.length - 1]!.x} ${lockConnector.points[lockConnector.points.length - 1]!.y} ` +
          `L ${lockConnector.railX} ${lockConnector.points[lockConnector.points.length - 1]!.y} ` +
          `L ${lockConnector.railX} ${lockConnector.points[0]!.y} ` +
          `L ${lockConnector.points[0]!.x} ${lockConnector.points[0]!.y}`;
  const lockGroupConnected = Boolean(lockConnector && lockConnectorPath);
  const isRecentRouteActive = (accountId: string): boolean => {
    const recentRouteAtMs = recentRouteActivityByAccount.get(accountId) ?? 0;
    return recentRouteAtMs > 0 && trafficNowMs - recentRouteAtMs <= recentRouteActiveWindowMs;
  };
  const isAccountTrafficActive = (accountId: string): boolean => {
    const account = accountsById.get(accountId);
    if (!account) {
      return false;
    }
    const upRecentlyActive =
      (account.trafficLastUpAtMs ?? 0) > 0 &&
      trafficNowMs - (account.trafficLastUpAtMs ?? 0) <= trafficActiveWindowMs;
    const downRecentlyActive =
      (account.trafficLastDownAtMs ?? 0) > 0 &&
      trafficNowMs - (account.trafficLastDownAtMs ?? 0) <= trafficActiveWindowMs;
    return upRecentlyActive || downRecentlyActive || isRecentRouteActive(accountId);
  };
  const lockGroupRoutingActive =
    lockGroupConnected &&
    lockGroupAccountIds.some((accountId) => {
      return isAccountTrafficActive(accountId);
    });
  const lockGroupTrafficIntensity = useMemo(() => {
    if (!lockGroupConnected || lockGroupAccountIds.length === 0) {
      return 0;
    }
    const recentRouteWeight = 0.6;
    let weightedActivity = 0;
    for (const accountId of lockGroupAccountIds) {
      const account = accountsById.get(accountId);
      if (!account) {
        continue;
      }
      weightedActivity += activityDecayStrength(account.trafficLastUpAtMs, trafficNowMs, trafficActiveWindowMs);
      weightedActivity += activityDecayStrength(account.trafficLastDownAtMs, trafficNowMs, trafficActiveWindowMs);
      const recentRouteAtMs = recentRouteActivityByAccount.get(accountId) ?? 0;
      weightedActivity += recentRouteWeight * activityDecayStrength(recentRouteAtMs, trafficNowMs, recentRouteActiveWindowMs);
    }
    const maxPossibleActivity = lockGroupAccountIds.length * (2 + recentRouteWeight);
    if (maxPossibleActivity <= 0) {
      return 0;
    }
    return Math.min(1, Math.max(0, weightedActivity / maxPossibleActivity));
  }, [
    accountsById,
    lockGroupAccountIds,
    lockGroupConnected,
    recentRouteActiveWindowMs,
    recentRouteActivityByAccount,
    trafficActiveWindowMs,
    trafficNowMs,
  ]);
  const lockGroupSignalStrengthRaw = lockGroupRoutingActive ? Math.max(0.18, lockGroupTrafficIntensity) : 0;
  const lockGroupSignalStrength = Math.round(lockGroupSignalStrengthRaw * 20) / 20;
  const lockConnectorStyle = lockGroupRoutingActive
    ? ({
        '--lock-signal-strength': lockGroupSignalStrength.toFixed(3),
      } as CSSProperties)
    : undefined;

  useEffect(() => {
    let clearChargeTimer: number | null = null;
    if (lockGroupRoutingActive && !previousLockGroupRoutingActiveRef.current) {
      setLockGroupChargeActive(true);
      clearChargeTimer = window.setTimeout(() => {
        setLockGroupChargeActive(false);
      }, 820);
    } else if (!lockGroupRoutingActive) {
      setLockGroupChargeActive(false);
    }
    previousLockGroupRoutingActiveRef.current = lockGroupRoutingActive;
    return () => {
      if (clearChargeTimer !== null) {
        window.clearTimeout(clearChargeTimer);
      }
    };
  }, [lockGroupRoutingActive]);

  return (
    <section className="pane object-pane" ref={paneRef}>
      <header className="section-header">
        <div>
          <p className="eyebrow">Accounts</p>
          <h1>Routing pool</h1>
        </div>
        <button className="tool-btn" type="button" onClick={onOpenAddAccount}>
          + Add
        </button>
      </header>
      <div className="pane-body">
        <div className="routing-pool-controls">
          <RoutingPoolSortSelect
            label="Sort"
            value={primarySortKey}
            options={[...ROUTING_POOL_SORT_OPTIONS]}
            onChange={(nextKey) => {
              const nextPrimary = nextKey as RoutingPoolSortKey;
              setSortKeys((previous) => {
                const previousSecondary = previous[1] ?? null;
                if (previousSecondary === null || previousSecondary !== nextPrimary) {
                  return previousSecondary === null ? [nextPrimary] : [nextPrimary, previousSecondary];
                }
                const replacement = ROUTING_POOL_SORT_OPTIONS.find((option) => option.key !== nextPrimary)?.key;
                return replacement ? [nextPrimary, replacement] : [nextPrimary];
              });
            }}
          />
          <button
            type="button"
            className="routing-sort-plus-btn"
            aria-label="Add secondary routing sort"
            disabled={!canAddSecondarySort}
            onClick={() => {
              if (!canAddSecondarySort) {
                return;
              }
              const firstSecondary = secondarySortOptions[0];
              if (!firstSecondary) {
                return;
              }
              setSortKeys([primarySortKey, firstSecondary.key]);
            }}
          >
            +
          </button>
          {secondarySortKey ? (
            <>
              <RoutingPoolSortSelect
                label="Then"
                value={secondarySortKey}
                options={secondarySortOptions}
                onChange={(nextKey) => setSortKeys([primarySortKey, nextKey as RoutingPoolSortKey])}
              />
              <button
                type="button"
                className="routing-sort-minus-btn"
                aria-label="Remove secondary routing sort"
                onClick={() => setSortKeys([primarySortKey])}
              >
                −
              </button>
            </>
          ) : null}
        </div>
        <span style={{ display: 'none' }}>Focus: {accounts.find((account) => account.id === selectedAccountId)?.name ?? 'all accounts'}</span>
        <div
          className={`accounts-list${lockGroupConnected ? ' lock-group-active' : ''}${lockGroupRoutingActive ? ' lock-group-routing-active' : ''}`}
          id="accountsList"
          ref={accountsListRef}
        >
          {lockConnector && lockConnectorPath ? (
            <svg
              className={`lock-group-connector${lockGroupRoutingActive ? ' routing-active' : ''}${lockGroupChargeActive ? ' charge-in' : ''}`}
              viewBox={`0 0 ${lockConnector.canvasWidth} ${lockConnector.canvasHeight}`}
              width={lockConnector.canvasWidth}
              height={lockConnector.canvasHeight}
              style={lockConnectorStyle}
              aria-hidden="true"
            >
              <path className="lock-group-connector-path" d={lockConnectorPath} />
              {lockGroupRoutingActive ? (
                <>
                  <path className="lock-group-connector-path-signal signal-down" d={lockConnectorFlowPath ?? ''} pathLength={100} />
                  <path className="lock-group-connector-path-signal signal-up" d={lockConnectorFlowPathReverse ?? ''} pathLength={100} />
                </>
              ) : null}
            </svg>
          ) : null}
          {sortedAccounts.map((account) => {
            const metric = metrics.get(account.id);
            const isRouted = account.id === routedAccountId;
            const lockGrouped = isLockGroupedAccount(account.id);
            const lockPoolActive = lockGroupAccountIds.length >= 2;
            const trafficVisibleForAccount = !lockPoolActive || lockGroupSet.has(account.id);
            const primaryUsage = planPrimaryUsagePercent(account);
            const primaryRemaining = primaryUsage === null ? 0 : Math.max(0, 100 - primaryUsage);
            const secondaryUsage =
              account.telemetryBacked && account.plan !== 'free' && account.hasSecondaryQuota ? clampPercent(account.quotaSecondary) : null;
            const secondaryRemaining = secondaryUsage === null ? 0 : Math.max(0, 100 - secondaryUsage);
            const recentRouteActive = isRecentRouteActive(account.id);
            const upActive =
              trafficVisibleForAccount &&
              (((account.trafficLastUpAtMs ?? 0) > 0 &&
                trafficNowMs - (account.trafficLastUpAtMs ?? 0) <= trafficActiveWindowMs) ||
                recentRouteActive);
            const downActive =
              trafficVisibleForAccount &&
              (((account.trafficLastDownAtMs ?? 0) > 0 &&
                trafficNowMs - (account.trafficLastDownAtMs ?? 0) <= trafficActiveWindowMs) ||
                recentRouteActive);
            const primaryUsedLabel = primaryUsage === null ? '—' : `${primaryUsage}%`;
            const secondaryUsedLabel = secondaryUsage === null ? '—' : `${secondaryUsage}%`;
            const latencyLabel = account.telemetryBacked && account.latency > 0 ? `${Math.round(account.latency)} ms` : '—';
            const stickyLabel = account.telemetryBacked ? `${Math.max(0, Math.round(account.stickyHit))}%` : '—';
            const failoverLabel = account.telemetryBacked ? `${Math.max(0, Math.round(account.failovers))}` : '—';
            const primaryWindowLabel = primaryQuotaWindowLabel(account);
            const resetCountdownLabel = formatResetCountdown(accountResetAtMsForSort(account), trafficNowMs);
            const primaryRemainingLabel =
              primaryUsage === null ? '—' : `${primaryRemaining}%${resetCountdownLabel ? ` (${resetCountdownLabel})` : ''}`;
            return (
              <div
                key={account.id}
                className={`account-item${account.id === selectedAccountId ? ' active' : ''}${isRouted ? ' routed' : ''}`}
                role="button"
                tabIndex={0}
                onClick={() => {
                  setOpenTooltipAccountId(null);
                  onSelectAccount(account.id);
                }}
                onKeyDown={(event) => {
                  if (event.key === 'Enter' || event.key === ' ') {
                    event.preventDefault();
                    setOpenTooltipAccountId(null);
                    onSelectAccount(account.id);
                  }
                }}
              >
                <span className={`account-traffic-edge upload${upActive ? ' active' : ''}`} aria-hidden="true" />
                <span className={`account-traffic-edge download${downActive ? ' active' : ''}`} aria-hidden="true" />
                <div className="account-top">
                  <span className="account-name" title={account.name}>{account.name}</span>
                  <div className="account-actions">
                    <button
                      type="button"
                      className={`account-info-btn${openTooltipAccountId === account.id ? ' active' : ''}`}
                      aria-label={`Show usage details for ${account.name}`}
                      aria-expanded={openTooltipAccountId === account.id}
                      onClick={(event) => {
                        event.preventDefault();
                        event.stopPropagation();
                        setOpenTooltipAccountId((previous) => (previous === account.id ? null : account.id));
                      }}
                    >
                      i
                    </button>
                    <button
                      type="button"
                      className={`account-pin-btn${account.pinned ? ' pinned' : ''}`}
                      aria-label={account.pinned ? 'Unpin account' : 'Pin account'}
                      onClick={(event) => {
                        event.preventDefault();
                        event.stopPropagation();
                        setOpenTooltipAccountId(null);
                        onSelectAccount(account.id);
                        onTogglePin(account.id, !account.pinned);
                      }}
                    >
                      <svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">
                        <path d="M8.75 3.75a.75.75 0 0 0 0 1.5h.8l.56 4.48-2.45 2.45a1.75 1.75 0 0 0 1.24 2.99h2.35v4.08a.75.75 0 0 0 1.5 0v-4.08h2.35a1.75 1.75 0 0 0 1.24-2.99l-2.45-2.45.56-4.48h.8a.75.75 0 0 0 0-1.5z" />
                      </svg>
                    </button>
                    <button
                      type="button"
                      className={`account-lock-btn${lockGrouped ? ' lock-grouped' : ''}${lockGrouped && lockGroupRoutingActive ? ' lock-group-routing' : ''}`}
                      aria-label="Toggle routing lock group (Shift+click to multi-add)"
                      ref={lockButtonRef(account.id)}
                      onClick={(event) => {
                        event.preventDefault();
                        event.stopPropagation();
                        setOpenTooltipAccountId(null);
                        onSelectAccount(account.id);
                        setLockGroupAccountIds((previous) => {
                          let next: string[];
                          if (event.shiftKey) {
                            if (previous.includes(account.id)) {
                              next = previous.filter((value) => value !== account.id);
                            } else {
                              next = [...previous, account.id];
                            }
                          } else {
                            if (previous.includes(account.id)) {
                              next = previous.filter((value) => value !== account.id);
                            } else {
                              next = [account.id];
                            }
                          }
                          if (next.length >= 2) {
                            for (const candidate of accounts) {
                              if (candidate.pinned) {
                                onTogglePin(candidate.id, false);
                              }
                            }
                          }
                          void onUpdateLockedRoutingAccountIds(next);
                          return next;
                        });
                      }}
                    >
                      <svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">
                        <path d="M7 10V8a5 5 0 1 1 10 0v2h1a2 2 0 0 1 2 2v7a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2v-7a2 2 0 0 1 2-2zm2 0h6V8a3 3 0 0 0-6 0zm3 8.25a1.25 1.25 0 1 0 0-2.5 1.25 1.25 0 0 0 0 2.5" />
                      </svg>
                    </button>
                  </div>
                </div>
                <div className="account-status-row">
                  <span className={`traffic-indicator${upActive || downActive ? ' active' : ''}`} aria-hidden="true">
                    <span className={`traffic-arrow up${upActive ? ' active' : ''}`}>↑</span>
                    <span className={`traffic-arrow down${downActive ? ' active' : ''}`}>↓</span>
                  </span>
                  <span className={`account-status-meta${isRouted ? ' routed-active' : ''}`}>
                    <span className="account-plan">{account.plan}</span>
                    {isRouted ? <span className="routed-dot routed-dot-subtle" title="Routed account" aria-hidden="true" /> : null}
                  </span>
                </div>
                <div className="account-meta-row">
                  <span className="account-meta">
                    {primaryWindowLabel} left <strong>{primaryRemainingLabel}</strong>
                  </span>
                  <span className="account-meta">{metric?.capability ? 'eligible' : 'blocked'}</span>
                </div>
                <div className="quota-stack">
                  <div className="mini-bar quota-track" aria-label={`${primaryWindowLabel} quota remaining`}>
                    <div className={`mini-fill quota-fill${primaryUsage !== null && primaryUsage >= 80 ? ' hot' : ''}`} style={{ width: `${primaryRemaining}%` }} />
                  </div>
                  {secondaryUsage !== null ? (
                    <div className="mini-bar quota-track quota-secondary-track" aria-label="Weekly quota remaining">
                      <div
                        className={`mini-fill quota-fill quota-fill-secondary${secondaryUsage >= 80 ? ' hot' : ''}`}
                        style={{ width: `${secondaryRemaining}%` }}
                      />
                    </div>
                  ) : null}
                </div>
                <div
                  className={`account-stats-tooltip${openTooltipAccountId === account.id ? ' open' : ''}`}
                  role="tooltip"
                  aria-hidden={openTooltipAccountId === account.id ? 'false' : 'true'}
                >
                  <div className="account-stats-tooltip-title">{account.name}</div>
                  <div className="account-stats-tooltip-grid">
                    <div>
                      <span>{primaryWindowLabel} used</span>
                      <strong>{primaryUsedLabel}</strong>
                    </div>
                    {secondaryUsage !== null ? (
                      <div>
                        <span>Weekly used</span>
                        <strong>{secondaryUsedLabel}</strong>
                      </div>
                    ) : null}
                    <div>
                      <span>Requests 24h</span>
                      <strong>{Math.max(0, Math.round(account.routed24h))}</strong>
                    </div>
                    <div>
                      <span>Latency</span>
                      <strong>{latencyLabel}</strong>
                    </div>
                    <div>
                      <span>Sticky hit</span>
                      <strong>{stickyLabel}</strong>
                    </div>
                    <div>
                      <span>Failovers</span>
                      <strong>{failoverLabel}</strong>
                    </div>
                  </div>
                </div>
              </div>
            );
          })}
        </div>
      </div>
    </section>
  );
}
