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

The rules have one idea: a failure has to reach whoever can do something
about it, in a form they can use. An audit looks for failures that do not:

- A failure that is ours and never reaches us. It ends in a sentence to the
  person, a log line alone, a default value, or a callback with no handler
  above it.
- A failure that is the person's and is sent to us. It ends in "Something
  went wrong on our side" and a report, although the other side named a cause
  the person can act on.
- A failure that reaches the right one in a form they cannot use: a sentence
  that is untrue about where the problem lies or gives no next step, the
  other side's plain words thrown away, or a report without the other side's
  status, request id and error text.
- A failure that belongs to neither, such as a service the person chose being
  down, that is called ours or is reported.
- A branch for "anything else" that gives one outcome to failures with
  different owners.
- A failure handled on a guess. For each signal the code relies on, say how
  it was verified: the other side's documentation, or a real answer.
- A failure that is not written to the log.

Each of these is a finding whether or not production has shown it. Reading
does not find the failures nobody foresaw; the reports show those.

## Audit a commit range

Input: a repository and a range, such as `jusprin/v2.0.0..jusprin-dev`.

The audit is one question, asked of every place the changed code decides what
happened: **whose failure is this, and does it reach them in a form they can
use?** Answer it from the failure's cause and from what the other side
actually says, not from the code path, the status code, or whether a sentence
exists. Do not search for particular code shapes. Each failure this audit
exists for had a shape nobody had listed, and a list only finds what its
writer already thought of.

1. List the changed code: `git diff --stat <range>`, then read each changed
   function whole in JusPrin's own code (`src/slic3r/GUI/JusPrin/`, the
   pages, `web_ent/jusprin/`). Read the function, not the diff lines: what a
   change leaves the rest of the function to assume is where failures hide.
2. Wherever the code decides what happened (an outcome, a state, a record,
   what the person is told, whether work goes on), ask the question, then
   check what follows from the answer.
   - The person's: they are told, truthfully, where the problem lies and
     what to do, or shown the other side's own words with its name. It is
     not reported. Flag a sentence of ours with no test or without
     Kenneth's quoted yes in its commit message, a cause that is a status
     code, an exception type or "the request was not valid", and two
     sentences that leave the person doing the same thing.
   - Ours, or it cannot be told: the rules allow almost no code, so the code
     is the finding. Two things only are allowed:
     - turning a failure that arrived as a value into a thrown error on the
       spot, keeping the other side's status, request id and error text and
       the request's own facts (user, chat, turn, model);
     - the one handler at the top of an entry point, which reports through
       the reporting function and tells the person "Something went wrong on
       our side", with nothing more specific.
     Everything else is a finding: work that goes on after the failure, a
     value or a sentence invented for it, a retry, a report call anywhere
     but the top handler, a failure recorded as a success, and the other
     side's status, request id or error text dropped on the way up. A
     sentence, a code or a branch of its own for one kind of failure that is
     ours (behind a JusPrin account, a provider or our server slow,
     overloaded, down or limiting us) is a finding, even when it looks
     carefully designed.
   - Neither's: the person is told truthfully whose service is failing, and
     it is not reported.
3. Follow one failure up from each entry point the change touches and say
   where it ends: at a handler that reports it and tells the person, at a
   sentence, or nowhere.
4. Read every place that catches. Flag a catch that is neither recovering
   from a failure the code understands nor translating at a boundary and
   rethrowing. Read every new or changed entry in the server's
   expected-errors table as a sentence of ours (step 2).
5. Report each finding as `file:line`, whose failure it is and where it goes
   today, and what a person or the record would see go wrong. Say which
   findings you checked by running code and which only by reading. Code older
   than the range is not a finding of this audit; name it separately if it
   matters.
