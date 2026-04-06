import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it, vi } from 'vitest';
import { ErrorBoundary } from './ErrorBoundary';

function Thrower(): never {
  throw new Error('boom');
}

function Healthy() {
  return <div>healthy</div>;
}

function Harness({ shouldThrow }: { shouldThrow: boolean }) {
  return (
    <ErrorBoundary>
      {shouldThrow ? <Thrower /> : <Healthy />}
    </ErrorBoundary>
  );
}

describe('ErrorBoundary', () => {
  it('renders fallback on error and can reset', async () => {
    const consoleErrorSpy = vi.spyOn(console, 'error').mockImplementation(() => {});
    try {
      const { rerender } = render(<Harness shouldThrow />);

      expect(screen.getByText('Renderer crashed')).toBeInTheDocument();
      expect(screen.getByText('boom')).toBeInTheDocument();

      rerender(<Harness shouldThrow={false} />);
      await userEvent.click(screen.getByRole('button', { name: 'Reset UI' }));

      expect(screen.getByText('healthy')).toBeInTheDocument();
    } finally {
      consoleErrorSpy.mockRestore();
    }
  });
});
