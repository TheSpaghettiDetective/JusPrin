import type { ProjectInfo } from '../bridge/protocol';
import { projectHistoryLine } from '../history';
import { savedDate } from '../savedDate';
import { HistoryGlyph } from './Glyphs';

// A 4:3 thumbnail over a fixed footer: the title with when it was last saved,
// and the host's status line under it. Only a printing project is tinted and
// dotted -- a grey tint read as disabled and a filled status color hid the
// text, both tried and rejected in review.
//
// A project that ever sent or exported G-code has a print-history line under
// the footer (board history-05). It opens the history filtered to the project,
// so it is a control of its own beside the one that opens the project.
export function ProjectCard({
  project,
  onOpen,
  onHistory,
}: {
  project: ProjectInfo;
  onOpen: (id: string) => void;
  onHistory: (id: string) => void;
}) {
  const printing = project.status.kind === 'printing';
  const saved = project.updatedAt ? savedDate(project.updatedAt) : '';
  return (
    <div className={printing ? 'project-card printing' : 'project-card'}>
      <button type="button" className="project-card-open" onClick={() => onOpen(project.id)}>
        <span className="project-thumbnail">
          {project.thumbnailUrl ? (
            <img src={project.thumbnailUrl} alt="" />
          ) : (
            // A project without a preview keeps the same card height.
            <span className="project-thumbnail-empty" />
          )}
        </span>
        <span className="project-footer">
          <span className="project-title-row">
            <span className="project-title">{project.name}</span>
            {saved && <span className="project-date">{saved}</span>}
          </span>
          {project.status.text && (
            <span className="project-status">
              {printing && <span className="status-dot printing" aria-hidden="true" />}
              <span className="project-status-text">{project.status.text}</span>
            </span>
          )}
        </span>
      </button>
      {project.history && (
        <button type="button" className="project-history" onClick={() => onHistory(project.id)}>
          <HistoryGlyph />
          <span className="project-history-text">{projectHistoryLine(project.history)}</span>
        </button>
      )}
    </div>
  );
}
