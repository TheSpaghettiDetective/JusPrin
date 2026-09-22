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
    expect(screen.getByText('Your printer is saved. Connecting is optional.')).toBeInTheDocument();
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
    render(<PrinterConnection session={session({ candidates: [] })} onAction={onAction} />);
    fireEvent.click(screen.getByRole('button', { name: 'Sign in to Bambu' }));
    expect(onAction).toHaveBeenCalledWith('connection_sign_in');
  });
});

 it('names a known LAN printer without asking for another choice or account sign-in', () => {
   render(<PrinterConnection session={session({ deviceId: 'serial', candidates: [{ id: 'serial', name: 'Garage A1', address: '', lanMode: true }] })} onAction={vi.fn()} />);
   expect(screen.queryByRole('combobox')).not.toBeInTheDocument();
   expect(screen.getByText('Garage A1')).toBeVisible();
   expect(screen.getByRole('button', { name: 'Sign in to Bambu' })).not.toBeVisible();
   expect(screen.getByText('Find a printer in my Bambu account instead')).toBeVisible();
 });
 it('offers account sign-in without a LAN code for an account printer', () => {
   render(<PrinterConnection session={session({ candidates: [{ id: 'serial', name: 'Account printer', address: '', lanMode: false }] })} onAction={vi.fn()} />);
   expect(screen.getByRole('button', { name: 'Sign in to Bambu' })).toBeVisible();
   expect(screen.queryByLabelText('LAN access code')).not.toBeInTheDocument();
 });
 it('keeps a verified connection while offering correction for a reported nozzle mismatch', () => {
   const onAction = vi.fn();
   render(<PrinterConnection session={session({ state: 'verified', nozzleMismatch: true })} onAction={onAction} />);
   expect(screen.getByText(/different nozzle size/)).toBeVisible();
   fireEvent.click(screen.getByRole('button', { name: 'Printer settings' }));
   expect(onAction).toHaveBeenCalledWith('manual_setup');
   expect(screen.getByRole('button', { name: 'Done' })).toBeVisible();
 });
