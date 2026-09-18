import type { PrinterReceiptInfo } from '../bridge/protocol';
import { CloseGlyph } from './Glyphs';

// The strip above the list, right after "Add this printer" (WP6): what was
// saved and what it assumed, with the one thing left to do. It stays up
// until dismissed -- an add is easy to miss at the bottom of a long list,
// and the card itself only highlights for a moment.
export function PrinterReceipt({
  receipt,
  onChange,
  onDismiss,
}: {
  receipt: PrinterReceiptInfo;
  onChange: () => void;
  onDismiss: () => void;
}) {
  const facts = [receipt.nozzle, receipt.plate, receipt.filament].filter(Boolean).join(', ');
  return (
    <div className="printer-receipt" role="status">
      <p className="printer-receipt-text">
        <span className="printer-receipt-name">{receipt.name}</span> added · {facts}
        {receipt.assumed ? ' — assumed' : ''} · Not connected yet ·{' '}
        <button type="button" className="printer-receipt-change" onClick={onChange}>
          Change
        </button>
      </p>
      <button type="button" className="printer-receipt-dismiss" aria-label="Dismiss" onClick={onDismiss}>
        <CloseGlyph />
      </button>
    </div>
  );
}
