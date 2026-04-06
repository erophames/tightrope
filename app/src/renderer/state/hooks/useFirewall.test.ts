import { act, renderHook } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { useFirewall } from './useFirewall';

describe('useFirewall', () => {
  it('refreshes firewall mode and entries', async () => {
    const listFirewallIps = vi.fn().mockResolvedValue({
      mode: 'allowlist_active',
      entries: [{ ipAddress: '10.0.0.1', createdAt: 'now' }],
    });
    const addFirewallIp = vi.fn().mockResolvedValue(true);
    const removeFirewallIp = vi.fn().mockResolvedValue(true);

    const { result } = renderHook(() =>
      useFirewall({
        pushRuntimeEvent: vi.fn(),
        listFirewallIpsRequest: listFirewallIps,
        addFirewallIpRequest: addFirewallIp,
        removeFirewallIpRequest: removeFirewallIp,
      }),
    );

    await act(async () => {
      await result.current.refreshFirewallIps();
    });

    expect(result.current.firewallMode).toBe('allowlist_active');
    expect(result.current.firewallEntries).toHaveLength(1);
    expect(result.current.firewallEntries[0].ipAddress).toBe('10.0.0.1');
  });

  it('adds firewall IP and emits success status', async () => {
    const pushRuntimeEvent = vi.fn();
    const addFirewallIp = vi.fn().mockResolvedValue(true);
    const removeFirewallIp = vi.fn().mockResolvedValue(true);
    const listFirewallIps = vi.fn().mockResolvedValue({
      mode: 'allowlist_active',
      entries: [{ ipAddress: '10.0.0.5', createdAt: 'now' }],
    });

    const { result } = renderHook(() =>
      useFirewall({
        pushRuntimeEvent,
        listFirewallIpsRequest: listFirewallIps,
        addFirewallIpRequest: addFirewallIp,
        removeFirewallIpRequest: removeFirewallIp,
      }),
    );

    act(() => {
      result.current.setFirewallDraft('10.0.0.5');
    });

    await act(async () => {
      await result.current.addFirewallIpAddress();
    });

    expect(addFirewallIp).toHaveBeenCalledWith('10.0.0.5');
    expect(result.current.firewallDraftIpAddress).toBe('');
    expect(pushRuntimeEvent).toHaveBeenCalledWith('firewall allowlist added 10.0.0.5', 'success');
  });
});
