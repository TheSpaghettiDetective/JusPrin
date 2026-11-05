// The design system's Field/Verification Code: one input drawn as six cells.
// It takes digits only, a pasted code fills every cell, and the sixth digit
// sends the code. The input lies over the cells, transparent, so the system's
// own text handling (paste, autofill of one-time codes, deletion) stays.

import { forwardRef, useState } from 'react';

const LENGTH = 6;

interface Props {
  // The digits, owned by the caller so it can clear them for a new code.
  value: string;
  onChange: (value: string) => void;
  onComplete: (code: string) => void;
  // The server refused these digits: every cell is outlined in danger.
  refused?: boolean;
  disabled?: boolean;
}

export const CodeField = forwardRef<HTMLInputElement, Props>(function CodeField(
  { value, onChange, onComplete, refused, disabled },
  ref,
) {
  const [focused, setFocused] = useState(false);
  const cells = Array.from({ length: LENGTH }, (_, index) => value[index] ?? '');
  // The cell the next digit goes in carries the focus ring.
  const active = focused && value.length < LENGTH ? value.length : -1;
  return (
    <div className={refused ? 'code-field refused' : 'code-field'}>
      <input
        ref={ref}
        type="text"
        inputMode="numeric"
        autoComplete="one-time-code"
        aria-label="Sign-in code"
        aria-invalid={refused || undefined}
        value={value}
        disabled={disabled}
        onFocus={() => setFocused(true)}
        onBlur={() => setFocused(false)}
        onChange={(event) => {
          const digits = event.target.value.replace(/\D/g, '').slice(0, LENGTH);
          onChange(digits);
          if (digits.length === LENGTH && digits !== value) onComplete(digits);
        }}
      />
      {cells.map((digit, index) => (
        <span key={index} className={index === active ? 'code-cell active' : 'code-cell'} aria-hidden="true">
          {digit}
        </span>
      ))}
    </div>
  );
});
