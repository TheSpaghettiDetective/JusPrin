// What OrcaSlicer said while loading a file the person brought in, when the
// Agent could not speak about it: each dialog the app answered in their
// place, in OrcaSlicer's words, with what that answer left, and a way to set
// the Agent up so it can explain them.

import { FileReport, OrcaMessage } from '../bridge/protocol';

interface Props {
  report: FileReport;
  onSetUpAgent?: () => void;
}

// OrcaSlicer titles its dialogs "<app> - <title>"; the app name adds nothing
// inside the app.
function heading(title: string): string {
  return title.replace(/^JusPrin - /, '');
}

// What the answer given in the person's place did, when it did anything
// they should know about.
export function answerNote(message: OrcaMessage): string | undefined {
  if (message.recognized === false) return 'JusPrin closed this without answering it.';
  if (message.answer === 'no') return 'JusPrin answered No to this question.';
  if (message.answer === 'cancel') return 'JusPrin chose Cancel for this question.';
  return undefined;
}

function fileName(path: string): string {
  return path.split(/[\\/]/).pop() ?? path;
}

export function FileNotesCard({ report, onSetUpAgent }: Props) {
  const files = report.loads.flatMap((load) => load.files).map(fileName);
  return (
    <article className="history-card file-notes-card" aria-label="Notes about this file">
      <h3 className="file-notes-title">Notes about this file</h3>
      {files.length > 0 && <p className="file-notes-files">{files.join(', ')}</p>}
      <ul className="file-notes-list">
        {report.messages.items.map((message, index) => {
          const note = answerNote(message);
          return (
            <li key={index}>
              {message.title && <span className="file-notes-heading">{heading(message.title)}</span>}
              {message.text && <span className="file-notes-text">{message.text}</span>}
              {note && <span className="file-notes-answer">{note}</span>}
            </li>
          );
        })}
      </ul>
      {report.messages.truncated && <p className="file-notes-more">OrcaSlicer said more than fits here.</p>}
      <p className="file-notes-help">If you'd like help with these, set up the Agent: it can explain them and fix what needs fixing.</p>
      {onSetUpAgent && (
        <button type="button" className="primary" onClick={onSetUpAgent}>
          Set up the Agent
        </button>
      )}
    </article>
  );
}
