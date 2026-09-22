import { useEffect, useRef, useState } from 'react';
import type { PrinterSessionPayload } from '../bridge/protocol';

export function PrinterConnection({ session, onAction }: {
  session: PrinterSessionPayload;
  onAction: (action: string, id?: string, extra?: Record<string, string>) => void;
}) {
  const connection = session.connection;
  const [device, setDevice] = useState('');
  const [code, setCode] = useState('');
  const [address, setAddress] = useState(connection?.address ?? '');
  const [hostType, setHostType] = useState(connection?.hostType || 'moonraker');
  const heading = useRef<HTMLHeadingElement>(null);
  const send = useRef(onAction);
  send.current = onAction;
  const connecting = connection?.state === 'connecting';
  const verified = connection?.state === 'verified';
  const unavailable = connection?.state === 'unavailable';
  const host = connection?.provider === 'host';
  const added = session.added ?? [];
  const chosen = device || connection?.deviceId || (connection?.candidates.length === 1 ? connection.candidates[0].id : '');

  useEffect(() => { heading.current?.focus(); }, [connection?.name]);
  useEffect(() => {
    setDevice(''); setCode(''); setAddress(connection?.address ?? ''); setHostType(connection?.hostType || 'moonraker');
  }, [connection?.name]);
  useEffect(() => {
    if (!connecting) return;
    const timer = window.setInterval(() => send.current('connection_refresh'), 1000);
    return () => window.clearInterval(timer);
  }, [connecting]);

  return (
    <section className="printer-connection" aria-label="Printer setup">
      <h1 ref={heading} tabIndex={-1}>
        {added.length ? (added.length === 1 ? 'Your printer has been added' : 'Your printers have been added') : 'Connect your printer'}
      </h1>
      {added.map((printer) => (
        <div key={printer.name} className="printer-connection-receipt">
          <p>You can now prepare prints for <strong>{printer.name}</strong>.</p>
          {printer.model && printer.model !== printer.name && <p className="footnote">{printer.model}</p>}
          {!connection && (printer.connected ? <p>Connected. You can send files from JusPrin.</p> :
            <button type="button" className="primary" onClick={() => onAction('connect', printer.name)}>Connect printer</button>)}
        </div>
      ))}
      {!connection && <p>Connecting is optional. Where supported, you can send files directly from JusPrin. You can set this up later from your printer’s card.</p>}
      {connection && <>
        <h2>{connection.name}</h2>
        {verified ? <p role="status">{host ? 'Connection test succeeded. File sending is configured; live printer status is not shown here.' : 'Connected. You can send files from JusPrin.'} Starting a print is a separate action.</p> :
          connecting ? <p role="status">Connecting to your printer…</p> : <>
            {connection.message && <p role={connection.state === 'failed' ? 'alert' : 'status'}>{connection.message}</p>}
            {connection.state === 'failed' && added.length > 0 && <p>Your printer has been added, but we couldn’t connect to it. Retry the connection below.</p>}
            {!unavailable && <form onSubmit={(event) => {
              event.preventDefault();
              onAction('connection_start', '', host ? { hostType, address: address.trim(), accessCode: code } : { deviceId: chosen, accessCode: code });
              setCode('');
            }}>
              {host ? <>
                <p>Connect to the software that manages your printer. This checks the connection without sending a file or starting a print.</p>
                <label>Connection type
                  <select value={hostType} onChange={(event) => { setHostType(event.target.value); setCode(''); }}>
                    <option value="moonraker">Klipper / Moonraker</option>
                    <option value="octoprint">OctoPrint</option>
                  </select>
                </label>
                <label>Host name or IP address
                  <input value={address} autoComplete="off" onChange={(event) => { setAddress(event.target.value); setCode(''); }} />
                </label>
              </> : <>
              <p>Choose your printer on the network or your Bambu account. For a new LAN connection, turn on LAN mode on the printer and enter its access code.</p>
              <label>Printer
                <select value={chosen} onChange={(event) => { setDevice(event.target.value); setCode(''); }}>
                  <option value="">Choose a printer</option>
                  {connection.candidates.map((printer) => <option key={printer.id} value={printer.id}>
                    {printer.name} · {printer.address || printer.id}
                  </option>)}
                </select>
              </label>
              {connection.candidates.length === 0 && <p>No matching printers found. Check that your printer is on the same network. Account printers need Bambu sign-in.</p>}
              {!connection.signedIn && <button type="button" onClick={() => onAction('connection_sign_in')}>Sign in to Bambu</button>}
              </>}
              <label>{host ? 'API key (if required)' : 'LAN access code'}
                <input type="password" autoComplete="off" value={code} onChange={(event) => setCode(event.target.value)} />
              </label>
              <p className="footnote">{host ? 'Leave blank to reuse the saved key for the same address, or when no key is required.' : 'Leave blank for an account printer or to reuse a saved code.'} This credential is never sent to the AI assistant.</p>
              <div className="printer-connection-actions">
                <button type="submit" className="primary" disabled={host ? !address.trim() : !chosen}>{host ? 'Save and test connection' : 'Connect'}</button>
                {!host && <button type="button" onClick={() => onAction('connection_refresh')}>Refresh printers</button>}
              </div>
            </form>}
            {unavailable && host && <button type="button" onClick={() => onAction('manual_setup')}>Printer settings</button>}
          </>}
      </>}
      <button type="button" onClick={() => onAction('close')}>{verified ? 'Done' : connecting ? 'Back to printers' : 'Not now'}</button>
    </section>
  );
}
