// The shared progress track: 4 DIP from component.progress.height, a
// border/subtle track with an action/primary fill. Without a value it is the
// indeterminate form, a fixed fill at the centre of the track; nothing moves,
// so there is no motion to switch off.

interface Props {
  label: string;
  value?: number;
  max?: number;
}

export function Progress({ label, value, max }: Props) {
  const determinate = value !== undefined && max !== undefined && max > 0;
  const percent = determinate ? Math.min(100, Math.max(0, (100 * value) / max)) : 0;
  return (
    <div
      className={determinate ? 'jp-progress' : 'jp-progress jp-progress-indeterminate'}
      role="progressbar"
      aria-label={label}
      aria-valuemin={determinate ? 0 : undefined}
      aria-valuemax={determinate ? max : undefined}
      aria-valuenow={determinate ? value : undefined}
    >
      <span className="jp-progress-fill" style={determinate ? { width: `${percent}%` } : undefined} />
    </div>
  );
}
