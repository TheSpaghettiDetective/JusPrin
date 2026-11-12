// What the web view does with a drag the page leaves alone: WebView2 and
// WebKitGTK open a dropped file or link in place of the page, so a picture
// dropped beside the composer replaced the whole chat. Only the message box
// takes a file (components/Composer.tsx), and it cancels the event itself;
// this catches the rest after it, so a file dropped anywhere else does
// nothing. A text field keeps its own drop of text or a link; a file is never
// opened in place, wherever it lands. On macOS the page never sees a drag:
// the app opens the file itself, and AgentWebView stops that and asks the
// page where it landed (file_dropped in App.tsx).

function editable(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) return false;
  return target instanceof HTMLTextAreaElement || target instanceof HTMLInputElement || target.isContentEditable;
}

function guard(event: DragEvent): void {
  if (event.defaultPrevented) return;
  const files = event.dataTransfer?.types.includes('Files') ?? false;
  if (!files && editable(event.target)) return;
  event.preventDefault();
  if (event.dataTransfer) event.dataTransfer.dropEffect = 'none';
}

export function installDropGuard(target: Window): () => void {
  target.addEventListener('dragover', guard);
  target.addEventListener('drop', guard);
  return () => {
    target.removeEventListener('dragover', guard);
    target.removeEventListener('drop', guard);
  };
}
