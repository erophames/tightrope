import { useEffect, useId, useMemo, useRef, useState } from 'react';

export interface RoutingPoolSortOption {
  key: string;
  label: string;
  description: string;
}

interface RoutingPoolSortSelectProps {
  label: string;
  value: string;
  options: RoutingPoolSortOption[];
  onChange: (nextKey: string) => void;
}

export function RoutingPoolSortSelect({
  label,
  value,
  options,
  onChange,
}: RoutingPoolSortSelectProps) {
  const rootRef = useRef<HTMLDivElement | null>(null);
  const [open, setOpen] = useState(false);
  const listboxId = useId();

  const selected = useMemo(
    () => options.find((option) => option.key === value) ?? options[0] ?? null,
    [options, value],
  );

  useEffect(() => {
    if (!open) {
      return;
    }
    const handlePointerDown = (event: MouseEvent) => {
      const target = event.target;
      if (!(target instanceof Element)) {
        return;
      }
      if (rootRef.current?.contains(target)) {
        return;
      }
      setOpen(false);
    };
    const handleEscape = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        setOpen(false);
      }
    };
    window.addEventListener('mousedown', handlePointerDown);
    window.addEventListener('keydown', handleEscape);
    return () => {
      window.removeEventListener('mousedown', handlePointerDown);
      window.removeEventListener('keydown', handleEscape);
    };
  }, [open]);

  if (selected === null) {
    return null;
  }

  return (
    <div className={`routing-sort-menu${open ? ' open' : ''}`} ref={rootRef}>
      <button
        type="button"
        className="routing-sort-trigger"
        aria-haspopup="listbox"
        aria-expanded={open}
        aria-controls={listboxId}
        onClick={() => setOpen((previous) => !previous)}
      >
        <span className="routing-sort-trigger-prefix">{label}</span>
        <span className="routing-sort-trigger-value">{selected.label}</span>
        <span className="routing-sort-trigger-caret" aria-hidden="true">▾</span>
      </button>
      {open ? (
        <div className="routing-sort-popover" role="listbox" id={listboxId} aria-label={`${label} sort options`}>
          {options.map((option) => {
            const active = option.key === value;
            return (
              <button
                key={option.key}
                type="button"
                role="option"
                aria-selected={active}
                className={`routing-sort-option${active ? ' active' : ''}`}
                onClick={() => {
                  onChange(option.key);
                  setOpen(false);
                }}
              >
                <span className="routing-sort-option-label">{option.label}</span>
                <span className="routing-sort-option-description">{option.description}</span>
              </button>
            );
          })}
        </div>
      ) : null}
    </div>
  );
}
