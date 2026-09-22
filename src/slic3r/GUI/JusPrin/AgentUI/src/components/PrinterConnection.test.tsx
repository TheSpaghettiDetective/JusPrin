import { act, fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { PrinterConnection } from './PrinterConnection';
import type { PrinterConnectionInfo, PrinterSessionPayload } from '../bridge/protocol';

const session = (connection?: Partial<PrinterConnectionInfo>): PrinterSessionPayload => ({
  mode: 'connect', canAdd: false, blocks: [], facts: {} as PrinterSessionPayload['facts'],
  added: [{ name: 'Garage', model: 'A1 mini' }],
  connection: connection ? { name: 'Garage', provider: 'bambu', state: 'not_configured', message: '', deviceId: '',
    candidates: [{ id: 'serial', name: 'A1 mini', address: '192.168.1.2' }], ...connection } : null,
});

describe('printer addition and connection', () => {
  it('confirms the save and makes skipping connection explicit', () => {
    const onAction = vi.fn();
    render(<PrinterConnection session={{ ...session(), mode: 'add' }} onAction={onAction} />);
    expect(screen.getByRole('heading', { name: 'Your printer has been added' })).toHaveFocus();
    fireEvent.click(screen.getByRole('button', { name: 'Go to Home' }));
    expect(onAction).toHaveBeenCalledWith('close');
  });
  it('submits credentials only with a connection and clears the field', () => {
    const onAction = vi.fn();
    render(<PrinterConnection session={session({})} onAction={onAction} />);
    const input = screen.getByLabelText('LAN access code');
    fireEvent.change(input, { target: { value: 'secretcode' } });
    fireEvent.click(screen.getByRole('button', { name: 'Connect' }));
    expect(onAction).toHaveBeenCalledWith('connection_start', '', { deviceId: 'serial', accessCode: 'secretcode' });
    expect(input).toHaveValue('');
  });
  it('retains the saved receipt when the connection fails', () => {
    render(<PrinterConnection session={session({ state: 'failed', message: 'Check the access code.' })} onAction={vi.fn()} />);
    expect(screen.getByRole('alert')).toHaveTextContent('Check the access code.');
    expect(screen.getByRole('heading', { name: 'Your printer has been added' })).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Add this printer' })).not.toBeInTheDocument();
  });
  it('polls only during an active connection and stops after unmount', () => {
    vi.useFakeTimers();
    const onAction = vi.fn();
    const view = render(<PrinterConnection session={session({ state: 'connecting' })} onAction={onAction} />);
    act(() => vi.advanceTimersByTime(1000));
    expect(onAction).toHaveBeenCalledWith('connection_refresh');
    view.unmount();
    onAction.mockClear();
    act(() => vi.advanceTimersByTime(2000));
    expect(onAction).not.toHaveBeenCalled();
    vi.useRealTimers();
  });
  it('uses the host test without claiming live printer status', () => {
    render(<PrinterConnection session={session({ provider: 'host', state: 'verified' })} onAction={vi.fn()} />);
    expect(screen.getByRole('status')).toHaveTextContent('live printer status is not shown here');
    expect(screen.queryByRole('button', { name: 'Connect' })).not.toBeInTheDocument();
  });
  it('opens native Bambu sign-in without sending an agent message', () => {
    const onAction = vi.fn();
    render(<PrinterConnection session={session({})} onAction={onAction} />);
    fireEvent.click(screen.getByRole('button', { name: 'Sign in to Bambu' }));
    expect(onAction).toHaveBeenCalledWith('connection_sign_in');
  });
});
