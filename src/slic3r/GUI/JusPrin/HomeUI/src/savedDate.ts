import { t } from '@lingui/core/macro';
import { dateLocale } from '../../AgentUI/src/i18n';

export { dateLocale };

// When a project was last saved, as its card says it: "Today", "Yesterday",
// "Oct 24", or "Oct 24, 2025" outside this year. Days are the viewer's own,
// so a save late last night reads Yesterday, not Today. Empty for a time the
// page cannot read.
export function savedDate(iso: string, now: Date = new Date()): string {
  const when = new Date(iso);
  if (Number.isNaN(when.getTime())) return '';
  const midnight = (date: Date) => new Date(date.getFullYear(), date.getMonth(), date.getDate()).getTime();
  // Rounded, because a day that changes the clock is 23 or 25 hours long.
  const days = Math.round((midnight(now) - midnight(when)) / 86_400_000);
  if (days === 0) return t`Today`;
  if (days === 1) return t`Yesterday`;
  return when.toLocaleDateString(dateLocale('en-US'), {
    month: 'short',
    day: 'numeric',
    ...(when.getFullYear() === now.getFullYear() ? {} : { year: 'numeric' }),
  });
}
