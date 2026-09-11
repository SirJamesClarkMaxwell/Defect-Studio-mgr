---
name: dispatch-codex-task
description: Use when implementation work in this repo should be written by the Codex CLI rather than by hand - handing a bounded task to `codex exec`, writing the task file it works from, or verifying what it produced before merge.
---

# Dispatch a Codex task

## Overview

Codex writes the implementation; this session owns the contract and the verification. The split:

| Owner | Artifact |
|-------|----------|
| This session | branch, task file, `.hpp` signatures, failing GoogleTest cases |
| Codex | the `.cpp` that makes those tests pass |
| This session | build, architecture check, smoke test, review, merge |

Writing the header and the failing tests first is what makes the review tractable and keeps Codex
from drifting across layer boundaries — a header with no UI include cannot grow a UI dependency.

## When to use

- A workstream file under `docs/work/project/plans/` has been sliced into a task that fits one
  branch.
- The task touches implementation, not architecture decisions.

Do NOT use for: choosing an approach, splitting a plan, deciding layer boundaries, or anything where
the answer is a design, not a diff.

## Steps

### 1. Branch

```bash
git checkout -b task/NN-short-name
```

One branch per task (`AGENTS.md`, "Zasady pracy"). Never dispatch Codex onto `main`.

### 2. Write the task file

`docs/work/project/tasks/NN-short-name.md`, with these sections — all of them REQUIRED:

```markdown
# Task NN: <short name>

## Goal
<2-4 sentences. What works after this task that did not work before.>

## Files to create or change
<explicit list of paths>

## Files that must NOT be touched
<explicit list — always includes anything outside the task's layer>

## Acceptance criteria
<numbered, each one checkable by running something>

## Constraints
<layer boundaries, threading rules, build flags that apply>
```

The "must NOT be touched" section is the one Codex actually needs. Without it, a bounded task grows
into a refactor of whatever it read on the way.

### 3. Write the contract before dispatching

Create the `.hpp` with full signatures and the test file with failing cases. Build once to confirm
the tests compile and fail for the right reason. Only then dispatch.

### 4. Build the prompt file

Write it to the scratchpad, never inline — the prompt contains paths, backticks and non-ASCII, and
inline quoting breaks on Windows.

```markdown
Read `docs/work/project/tasks/NN-short-name.md` and implement it.

The header and the tests are already written and are the contract. Make the tests pass without
changing the header signatures or the test expectations. If you believe a signature is wrong, stop
and say so instead of changing it.

Repo rules that apply:
- Read `AGENTS.md` and `CLAUDE.md` first. The layer boundaries there are hard.
- Regenerate projects with `scripts/Windows/GenerateProjects.bat` after adding any new `.cpp`/`.hpp`
  (premake globs sources at generation time).
- `.cpp` files stay under ~500 lines.
- Do not create a parallel system next to one that already exists. Search first, extend second.

When done, report: files changed, tests now passing, anything you could not do.
```

### 5. Dispatch

```bash
cd <repo root>
cat <scratchpad>/prompt.md | codex exec -C . -s workspace-write -o <scratchpad>/codex-result.md -
```

- `-s workspace-write` — Codex may edit the repo and run commands. Use `read-only` for a review-only
  pass.
- `-o <file>` — final message goes to a file instead of being lost in the stream.
- `-` — prompt from stdin. Never pass a long prompt as an argument.
- `codex exec resume --last` continues the same session with a follow-up instead of starting cold.

Run it in the background; it takes minutes, not seconds.

### 6. Verify — all four, every time

1. `full-build-verify` skill. Release only during active development. Expected: 2 skipped tests
   (`ConPtyProcessTests.RunsCommandAndProducesOutput`,
   `BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable`) — that is the
   `DS_PYTHON_CAPI_AVAILABLE=0` skip count, not a regression.
2. `architecture-boundary-review` skill. Cheap grep check of the six hard boundaries. This is the
   dimension AI-written code breaks most quietly.
3. Run the app and exercise the feature by hand. Tests do not catch "the gizmo no longer responds to
   clicks".
4. Read the diff. `git diff main...HEAD --stat` first, then the files.

### 7. Merge

Only after all four pass. Then update the workstream status table in the plan README.

## Red flags — do not merge

- Codex changed a signature in the header you wrote
- Codex changed a test expectation to make it pass
- A new file appeared that duplicates something already in the repo
- Files outside the task's "files to change" list are in the diff
- You skipped the manual app run because "the tests are green"

Each of these means: read the diff properly, then send a follow-up with
`codex exec resume --last` naming the specific problem.

## Common mistakes

| Mistake | Fix |
|---------|-----|
| Handing Codex the whole plan | Hand it one task file. The plan is context you already hold. |
| Prompt inline on the command line | Write it to a file, pipe via stdin. |
| Dispatching before tests exist | The tests are the contract. No contract, no dispatch. |
| Accepting the final report as verification | The report says what Codex believes. Run the build. |
| Forgetting `GenerateProjects.bat` after new files | Build fails or silently omits the new `.cpp`. |
