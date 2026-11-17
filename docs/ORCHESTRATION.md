# Orchestration guide

How the work is run: one **orchestrator** (Fable) plans and coordinates, and **worker agents** (Opus 5.5) implement, review and verify. The human owns product decisions and all on-device testing.

## Where things live

| What | Where | Why |
|---|---|---|
| Project facts and rules every agent must follow | `CLAUDE.md` (plus the platform notes it imports) | Loaded in every session, including workers |
| Role definitions for workers | `.claude/agents/*.md` (drafts below) | Reusable, versioned, keeps CLAUDE.md short |
| The current task | The task brief the orchestrator writes | Workers do not see the orchestrator's conversation, so a brief must stand alone |
| Progress and decisions | `docs/STATUS.md` (orchestrator maintains) | Survives session restarts and lets the human check in |

Keep CLAUDE.md for facts and rules. Keep workflow in this file and in the agent definitions.

## Orchestrator rules

1. Read `CLAUDE.md` and `docs/PLATFORM_NOTES.md` first. Then plan before delegating.
2. Break work into small modules with clear interfaces. **One worker owns one module or file set at a time**, so workers do not edit the same files.
3. Write every task brief with the template below. A worker that has to guess is a failed brief.
4. Never mark work done on a worker's say-so. Every implementation goes to a **separate reviewer** that did not write it, and every claim of "works" needs a command or test that shows it.
5. The orchestrator integrates, resolves conflicts, and keeps `docs/STATUS.md` current.
6. Stop and ask the human before: choosing a licence-affecting dependency, copying GPL code, changing the control scheme, changing a milestone's scope, or anything that needs the device.
7. Hardware tests are run by the human. Agents prepare a short numbered checklist and the exact build artefact, then wait for the results.

## Task brief template

```
Goal: <one sentence outcome>
Context: <what exists, which files, which docs to read>
Scope: <files/modules you may change>   Out of scope: <what you must not touch>
Constraints: <real-time rules, licensing, interfaces to preserve>
Acceptance criteria: <observable, testable statements>
How to verify: <exact commands/tests to run and what passing looks like>
Report back: <what to include: files changed, test output, open questions>
```

## Review gates

- **Code review** (fresh agent): correctness, real-time safety (any allocation, lock, I/O or unbounded loop in the audio path is a blocking finding), interface adherence, licence compliance, readability.
- **DSP check**: render to WAV on the host and inspect: no clicks at parameter changes, no DC offset, no NaN/inf, levels sane, aliasing checked at high notes.
- **Milestone gate**: tests pass, `THIRD_PARTY.md` is current, `docs/STATUS.md` updated, human checklist prepared if the milestone touches the device.

## Draft worker definitions

Save as `.claude/agents/implementer.md` and `.claude/agents/reviewer.md`. Adjust the frontmatter to the current Claude Code format if it differs.

`implementer.md`
```
---
name: implementer
description: Implements one scoped module from a task brief. Use for writing code and tests.
model: opus
---
You implement exactly what the task brief asks, within its scope. Follow CLAUDE.md and the
real-time audio rules. Write tests alongside code. Run the verification commands from the brief
and include their real output in your report. If the brief is ambiguous or a constraint cannot
be met, stop and report the problem instead of improvising. Do not edit files outside scope.
```

`reviewer.md`
```
---
name: reviewer
description: Reviews a change with fresh eyes. Use after any implementation, before it is accepted.
model: opus
---
You review changes you did not write. Read CLAUDE.md first. Check correctness, real-time
safety in the audio path, interface adherence, licensing, and test quality. Run the tests
yourself. Report findings ranked by severity with file and line, and say what you verified
versus what you assumed. Do not fix code unless asked.
```
