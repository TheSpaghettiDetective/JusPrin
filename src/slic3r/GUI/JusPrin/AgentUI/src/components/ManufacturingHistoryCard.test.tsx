import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { PhysicalPrintInfo } from '../bridge/protocol';
import { ManufacturingHistoryCard } from './ManufacturingHistoryCard';

const print: PhysicalPrintInfo = {
  id: 'p-1', seq: 1, startedAt: '2026-10-01T13:09:00Z', endedAt: '2026-10-01T14:13:00Z',
  outcome: 'failed', failure: 'Layer shift reported near layer 62.', buildId: 'b-1', projectId: 'project-1',
  conversationId: 'conv-1', afterMessageId: 'm-1', plateIndex: 0, plateName: 'Plate 1',
  printer: 'Bambu X1C', material: 'Gray PETG', manufacturingInputHash: 'a'.repeat(64),
  outputHash: 'b'.repeat(64), gcodeHash: 'c'.repeat(64), stoppedPercent: 41,
  statistics: { printTimeSeconds: 9360, filamentMm: 1842.5, materialGrams: 68, materialCost: 1.12, layerCount: 181 },
};

describe('failed print timeline card', () => {
  it('shows the failure, retains its details, and starts a discussion when asked', () => {
    const onDiscussFailure = vi.fn();
    const { container } = render(<ManufacturingHistoryCard entry={{ kind: 'print', seq: 1, afterMessageId: 'm-1', record: print }}
      onDiscussFailure={onDiscussFailure} />);

    expect(screen.getByText('Print failed')).toBeInTheDocument();
    expect(screen.getByText('Bambu X1C · stopped at 41%, 1h 04m in')).toBeInTheDocument();
    expect(container.querySelector('.failure-reason')).toHaveTextContent('Layer shift reported near layer 62.');
    fireEvent.click(screen.getByRole('button', { name: 'Discuss this failure' }));
    expect(onDiscussFailure).toHaveBeenCalledTimes(1);
    expect(onDiscussFailure).toHaveBeenCalledWith(print);

    fireEvent.click(screen.getByText('Print failed'));
    expect(container.querySelector('details')).toHaveAttribute('open');
    expect(screen.getByText('Printed G-code SHA-256')).toBeInTheDocument();
  });
});
