// Compact header mirroring the native workspace context the Agent sees. This
// is display-only: the native canvas remains the place to act on the project.

import { WorkspaceContext } from '../bridge/protocol';

export function ContextSummary({ context }: { context: WorkspaceContext | null }) {
  if (!context) return null;

  const active = context.plates.find((plate) => plate.active);
  const selectedNames =
    context.selection.status === 'objects'
      ? context.plates
          .flatMap((plate) => plate.objects)
          .filter((object) => context.selection.objectIds.includes(object.id))
          .map((object) => object.name)
      : [];

  const parts: string[] = [];
  if (context.printer.preset) parts.push(context.printer.preset);
  if (active) parts.push(`${active.name} · ${active.objects.length} object${active.objects.length === 1 ? '' : 's'}`);
  if (context.selection.status === 'objects' && selectedNames.length > 0)
    parts.push(`Selected: ${selectedNames.join(', ')}`);
  else if (context.selection.status === 'unsupported') parts.push('Selection: mixed');

  return (
    <div className="context-summary" data-testid="context-summary">
      <span className="project">{context.projectName || 'Untitled'}</span>
      {context.projectDirty ? ' •' : ''}
      {parts.length > 0 ? ` — ${parts.join(' · ')}` : ''}
    </div>
  );
}
