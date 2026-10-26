import type { ProjectInfo } from '../bridge/protocol';
import { savedDate } from '../savedDate';

// A 4:3 thumbnail over a fixed footer: the title with when it was last saved,
// and the host's status line under it. Only a printing project is tinted and
// dotted -- a grey tint read as disabled and a filled status color hid the
// text, both tried and rejected in review.
export function ProjectCard({ project, onOpen }: { project: ProjectInfo; onOpen: (id: string) => void }) {
  const printing = project.status.kind === 'printing';
  const saved = project.updatedAt ? savedDate(project.updatedAt) : '';
  return (
    <button
      type="button"
      className={printing ? 'project-card printing' : 'project-card'}
      onClick={() => onOpen(project.id)}
    >
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
  );
}
