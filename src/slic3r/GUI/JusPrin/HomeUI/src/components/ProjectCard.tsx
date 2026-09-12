import type { ProjectInfo } from '../bridge/protocol';

// A 4:3 thumbnail over a fixed footer: title on one line, status under it.
// Only a printing project is tinted and dotted -- a grey tint read as disabled
// and a filled status color hid the text, both tried and rejected in review.
export function ProjectCard({ project, onOpen }: { project: ProjectInfo; onOpen: (id: string) => void }) {
  const printing = project.status.kind === 'printing';
  return (
    <button
      type="button"
      className={printing ? 'project-card printing' : 'project-card'}
      title={project.path}
      onClick={() => onOpen(project.id)}
    >
      <span className="project-thumbnail">
        {project.thumbnailUrl ? (
          <img src={project.thumbnailUrl} alt="" />
        ) : (
          // A project whose .3mf carries no thumbnail keeps the same frame, so
          // one missing image cannot change the height of its row.
          <span className="project-thumbnail-empty" />
        )}
      </span>
      <span className="project-footer">
        <span className="project-title">{project.name}</span>
        <span className="project-status">
          {printing && <span className="status-dot printing" aria-hidden="true" />}
          {project.status.text}
        </span>
      </span>
    </button>
  );
}
