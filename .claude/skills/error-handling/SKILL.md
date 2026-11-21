---
name: error-handling
description: Use when asked to audit code for JusPrin's error-handling rules (the Error handling section of AGENTS.md in JusPrin, CLAUDE.md in tsd-enterprise) for a commit range in either repository. Run only when asked; nothing schedules it. The production check of JusPrin failures is the jusprin-production-check skill in tsd-enterprise.
---

# JusPrin error handling: audit

The rules are the Error handling section of `AGENTS.md` (JusPrin, the desktop
app) and of `CLAUDE.md` (tsd-enterprise, the JusPrin Cloud server). Read that
section first; this skill only says how to check code against it. Checking
production for failures over a period is the `jusprin-production-check` skill
in the tsd-enterprise repository.

The server's expected-errors table is `EXPECTED` in
`web_ent/jusprin/v2/errors.py`; the app has no table yet. The app reports
through `report_unexpected_error`, the server through Sentry. List an outcome
that looks designed but has no table entry apart from the other findings.

## What an audit checks

Four things, from "Two kinds of failure" in the rules:

- The reporting is complete. A failure that ends in a log line alone, a
  default value, a sentence for "anything else", or a callback with no
  handler above it is a hole.
- No expected error is handled as an unexpected one: a condition we can name
  and the person can act on (a rejected key, no credits, a chat too long)
  that ends in the generic sentence. This is a finding whether or not
  production has shown it.
- No expected error is handled on a guess. Say how the signal was verified:
  the other side's documentation, or a real answer.
- Every failure is written to the log, expected or unexpected.

Reading does not find the surprises themselves. The reports show those.

## Audit a commit range

Input: a repository and a range, such as `jusprin/v2.0.0..jusprin-dev`.

The audit is one question, asked of every place the changed code decides what
happened: **can the person do something about it, and did Kenneth decide what
they are told?** Do not search for particular
code shapes. Each failure this audit exists for had a shape nobody had listed,
and a list only finds what its writer already thought of.

1. List the changed code: `git diff --stat <range>`, then read each changed
   function whole in JusPrin's own code (`src/slic3r/GUI/JusPrin/`, the
   pages, `web_ent/jusprin/`). Read the function, not the diff lines: what a
   change leaves the rest of the function to assume is where failures hide.
2. Wherever the code decides what happened (an outcome, a state, a record,
   what the person is told, whether work goes on), ask the question.
   - Yes to both: there is an entry in the expected-errors table for that
     named cause. Go to step 4.
   - Otherwise the code is dealing with an unexpected error. Go to step 3.
     A sentence, a code or a branch of its own for a failure the person can
     do nothing about (a provider or our server slow, overloaded, down or
     limiting us) is a finding, even when it looks carefully designed.
3. For an unexpected error the rules allow almost no code, so the code is
   the finding. Two things only are allowed:
   - turning a failure that arrived as a value into a thrown error on the
     spot, keeping the other side's status, request id and error text and
     the request's own facts (user, chat, turn, model), with no
     interpretation;
   - the one handler at the top of an entry point, which reports through the
     reporting function and tells the person "Something went wrong on our
     side", with nothing more specific.
   Everything else is a finding: work that goes on after a failure nobody
   decided on, a value or a sentence invented for it, a retry, a report call
   anywhere but the top handler, a failure recorded as a success, and the
   other side's status, request id or error text dropped on the way up. Also
   follow one failure up from each entry point the change touches and say
   whether it reaches a handler that reports it and tells the person.
4. Read every new or changed entry in the expected-errors tables, and every
   place that catches. Flag an entry whose cause is a status code, an
   exception type or "the request was not valid", an entry the person can do
   nothing about, two entries that leave the person doing the same thing, an
   entry with no test, and
   an entry added without Kenneth's quoted yes in its commit message. Flag a
   catch that is neither recovering from an expected error nor translating
   at a boundary and rethrowing.
5. Report each finding as `file:line`, the rule it breaks, and what a person
   or the record would see go wrong. Say which findings you checked by
   running code and which only by reading. Code older than the range is not
   a finding of this audit; name it separately if it matters.
