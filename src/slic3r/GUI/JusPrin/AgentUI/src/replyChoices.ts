// The replies an assistant offers to tap. Its instructions tell it to end a
// message with one line, "Choices: first | second | third"; the page draws
// that line as chips instead of text. A message without it is just text, and
// the person types their answer.
//
// The model does not always break the line: "Set it to 0.6? Choices: Yes |
// Not yet" is the same offer, so the choices are read from the last line
// wherever "Choices:" starts a word in it.

const CHOICES = /(^|\s)Choices:(.*)$/;
// While streaming: the end of the text that is, or may yet become, "Choices:".
const PARTIAL = /(^|\s)(C(h(o(i(c(e(s(:.*)?)?)?)?)?)?)?)$/;

function lastLine(text: string): { before: string; line: string } {
  const trimmed = text.trimEnd();
  const cut = trimmed.lastIndexOf('\n');
  return { before: trimmed.slice(0, cut + 1), line: trimmed.slice(cut + 1) };
}

// A finished message: its text without the choices, and the choices.
export function splitChoices(text: string): { body: string; choices: string[] } {
  const { before, line } = lastLine(text);
  const found = CHOICES.exec(line);
  if (!found) return { body: text, choices: [] };
  const choices = found[2]
    .split('|')
    .map((choice) => choice.trim())
    .filter(Boolean);
  return { body: (before + line.slice(0, found.index)).trimEnd(), choices };
}

// A message still streaming: the same text, minus an end that is, or may yet
// become, the choices, so it never shows as text first.
export function withoutPartialChoices(text: string): string {
  const { before, line } = lastLine(text);
  const found = CHOICES.exec(line) ?? PARTIAL.exec(line);
  return found ? (before + line.slice(0, found.index)).trimEnd() : text;
}
