# Test suite review — are we testing right at each level?

Repository: `Defect-Studio-mgr`
Date: 2026-09-01
Scope: existing suite. What each level actually covers, what is missing, and the one move worth
making.

## Evidence portfolio

**System context:** A single-user desktop scientific workbench (C++ modular monolith, one process,
one executable), solo-maintained with AI assistance. No network service, no tenancy, no deployment
topology, no external consumers. Most failures are reversible — restart the app. **One class is
not:** silently producing wrong science. A bad bond regeneration, a botched undo, or a mangled
import can be saved, published on, and never noticed. That asymmetry drives everything below.

**Assumptions (provisional — say so where it matters):**
- **No CI service is configured in this repository.** No `.github/`, no `.gitlab-ci.yml`, no CI YAML
  anywhere outside `Vendor/`. Validation runs through local scripts (`TestBuildMatrix`,
  `TestReleaseDistTests`). If CI exists outside the repo, secondary observation 2 does not apply.
- **Test runtime is unmeasured.** No built test binary is present, so counts below are measured and
  runtimes are unknown. I have not guessed them.

**Critical failure surfaces**, in the project's own terms:
1. Wrong science produced silently — bonds, fractional↔cartesian conversion, defect construction,
   structure comparison, isosurface meshing.
2. Undo/redo corrupting a structure — the user edits, undoes, and the model does not come back.
3. Import/export losing or mangling data — YAML config, atom styles, VASP/POSCAR via the Python
   bridge.
4. Python runtime unavailable, hung, or silently absent.
5. UI interaction defects — gizmos, picking, selection.
6. GPU/render correctness.

| Risk or claim | Selected evidence | Why it belongs | Cost accepted | Deliberately omitted | Reopen when |
|---|---|---|---|---|---|
| Wrong science (bonds, lattice math, defects, comparison, meshing) | Unit tests, sociable, real collaborators — `tests/Domain` (31), `tests/Renderer/Scene` (34) | Pure functions with real invariants; fastest possible feedback and the domain math is an independent oracle | Milliseconds; near-zero maintenance | Broad integration — adds nothing to a pure calculation | A domain defect escapes a green module |
| **Undo/redo corrupts the structure** | **None today** | — | — | — | **Now — see primary recommendation** |
| Config/style/keymap persistence round-trip | Integration against the **real filesystem** — `tests/IO/*IoEventsTests`, `tests/Core/ConfigManagerTests` | Serialization and file lifecycle fail only against real I/O; a mocked stream tests the mock | Temp-dir setup, slower than unit | Mocked filesystem — would verify the double | Format or migration bugs escape |
| Python bridge produces correct structures | Integration with a **real interpreter and real pymatgen** — `tests/ScientificRuntime` (9) | The bridge's risk is the boundary itself, not the C++ around it | Requires a provisioned Python env; skips when absent | Mocked bridge — would prove nothing about pymatgen | See secondary observation 3 |
| Subprocess control (timeout, cancel, ConPTY) | Integration spawning **real processes** — `ProcessRunnerTests`, `ConPtyProcessTests`, `InteractiveProcessTests` | Process lifecycle and cancellation are OS behaviour | Platform-sensitive, slower | A fake process runner | Cancellation fails on a real long script |
| Concurrency: job scheduling, cancellation, nesting, resume | Unit + threading tests — `tests/Core/JobSystem` (8 files) | Concurrency defects are real, expensive, and untraceable in production | Some inherent nondeterminism risk | Load/soak testing — no throughput claim exists | A named throughput or latency target appears |
| UI interaction (gizmo drag, picking) | Pure-geometry unit tests — `SelectionHitTestTests` (10) | The *math* under the interaction is extractable and cheap; the ImGui drawing is not | Leaves drawing code unverified — accepted | Driving ImGui in a harness; screenshot/approval tests | Picking or gizmo defects start escaping |
| GPU/render correctness | **Nothing — deliberate** | No cheap reliable oracle; failures are visible immediately to the one user | Renders are eyeballed | Image-diff/golden-frame tests — high maintenance, brittle across drivers | Rendering ships to users who are not the author |
| Whole-app journeys | **No E2E — gate not met** | No journey names a failure that requires complete wiring and cannot be reached more cheaply | — | An ImGui-driving E2E harness | A named cross-system journey acquires material risk |
| Semantic/stochastic quality | **Not applicable** | The Python side is deterministic scientific computation, not ML or LLM | — | Evaluations, LLM judges | An ML or LLM surface is introduced |

**Deterministic tests vs stochastic evaluations:** the system has **no stochastic surface**. pymatgen
is deterministic numerics. Everything here is asserted, nothing is evaluated. No evaluation harness
should be bought.

**Generated-test oracle assessment:** **no oracle problems found — this is the suite's strongest
property.** I sampled across all six modules and found no snapshot, approval, golden-file, or
recorded-output tests, and no test whose expectation appears to have been read off the
implementation. Assertions encode domain requirements: a cubic cell has 12 edges and specific
reciprocal-lattice values (`StructureToRendererTests.cpp:44-46`), upsampling preserves corners and
interpolates midpoints, `DoesNotMatchTheSameReferenceAtomTwice`,
`ClosestPointsRaySegmentFindsTrueClosestPointOnNonUnitObliqueSegment`. Test names state requirements
rather than method names. **Coverage theatre is absent** — the failure mode most suites of this size
exhibit.

**Primary recommendation:** Add round-trip tests for the twelve undoable structure-edit commands —
`apply → undo` must restore the structure exactly — because that is the only untested code that can
silently corrupt the scientific source of truth, and the invariant is a free independent oracle.

**Consequences and trade-offs:** this portfolio owns two provisioned environments (a Python env with
pymatgen, and per-platform process/ConPTY behaviour) and deliberately leaves ImGui drawing code and
GPU output unverified. That is the right trade for a single-user desktop tool whose author sees every
frame — but it means UI regressions are found by using the app, not by CI, and that is a conscious
purchase, not an oversight.

**The smallest-portfolio check:** nothing here adds a level. No E2E (the gate requires a named
journey *and* a failure needing full wiring — neither exists), no contract tests (one deployable, one
team; the independence gate never opens), no evaluations (no stochastic surface), no load testing (no
named latency or throughput claim), no mutation tooling (close the gap first). The single addition is
unit-scoped and runs in milliseconds.

## Observed current portfolio

**247 tests · 6,044 lines · GoogleTest · classified by what they actually touch, not by directory:**

- **Unit (in-process, no I/O):** ~230 tests. Core 149 · Renderer 34 · Domain 31 · Presentation 15.
- **Integration (real filesystem, real process spawn, real Python):** 9 files —
  `ConfigManagerTests`, `ApplicationConfigControllerTests`, `AssetManagerTests`, `ProcessRunnerTests`,
  `ConPtyProcessTests`, `InteractiveProcessTests`, the three `IO/*IoEventsTests`,
  `PymatgenBridgeTests`, `ScriptRunnerTests`.
- **E2E:** zero — correctly (see the gate above).
- **Evaluations:** zero — correctly (no stochastic surface).
- **Test doubles:** **zero occurrences** of gmock, `MOCK_METHOD`, or hand-rolled Mock/Fake/Stub types
  anywhere in `tests/`.
- **CI wall-clock:** unknown — no CI configuration, no built binary to time.

Coverage weight by module (measured lines):

| Module | src lines | test lines | ratio | tests |
|---|---|---|---|---|
| Domain | 1,581 | 594 | **0.38** | 31 |
| Core | 11,256 | 3,990 | **0.35** | 149 |
| IO | 2,218 | 383 | 0.17 | 9 |
| ScientificRuntime | 2,603 | 242 | 0.09 | 9 |
| Renderer | 13,485 | 631 | 0.05 | 34 |
| Presentation | 20,827 | 204 | **0.01** | 15 |
| App | 6,986 | **0** | 0 | 0 |
| Events | 743 | 0 | 0 | 0 |

`Renderer` + `Presentation` are 34,312 lines — **62% of the source, holding 2.4% of the test lines.**

**That imbalance is mostly correct, and should not be "fixed".** Most of those 34k lines are ImGui
drawing code with no cheap oracle, and the suite already extracts the testable parts and tests them
well — `SelectionHitTestTests` covers ray/segment/triangle geometry, `SceneSystemTests` covers
entity sync. The instinct is right. Chasing the ratio would buy expensive, brittle evidence for
failures the single user sees immediately. **One specific piece of that 34k is a genuine gap**, and
it is not drawing code.

## Evidence-placement problems

**1. The twelve undoable structure-edit commands have zero tests.**
`src/Renderer/Commands/RendererAtomEditCommands.cpp` — 1,531 lines, 12 `ICommand` subclasses. Nothing
in `tests/` references `AtomEdit`, `DeleteSelectedAtoms`, `PasteAtoms`, `ApplyVacancy`,
`TransformSelectedAtoms`, `ChangeSelectedAtomType`, or `AddAtomAtCoordinates`.

This code is not ImGui drawing — it is pure logic over domain data, and it is **the only code in the
application that mutates the scientific source of truth.** `tests/Core/Undo/UndoStackTests.cpp` has
4 tests, but they test the *stack* (index movement, merging, grouping, clean-state tracking), not any
command's `Undo()`.

The design review found each of the 12 hand-rolls its own snapshot/restore — and that **2 of the 12
snapshot only `atoms`, not `bonds`** (`:483` `TransformSelectedAtomsCommand`, `:956`
`ChangeSelectedAtomTypeCommand`). That is either a deliberate optimisation or a bug that loses bond
state on undo. **No test distinguishes those two readings.** For a defect-analysis tool, "undo left
the bonds wrong and nobody noticed" is exactly the irreversible-failure class named at the top.

**2. `App` has 6,986 lines and zero direct tests.** Chiefly `YamlConfigSerializer.cpp` (1,943 lines).
It is exercised indirectly through `tests/Core/ConfigManagerTests.cpp`, which is legitimate sociable
testing — but a 1,943-line serializer is a boundary with a free round-trip oracle and no test that
names it. Lower priority than finding 1 only because a corrupt config is recoverable (delete the
file) while a corrupt structure is not.

**3. Nine `GTEST_SKIP` sites hide whether the Python bridge ran at all.**
`tests/ScientificRuntime/PymatgenBridgeTests.cpp:30`, `ScriptRunnerTests.cpp:21`,
`BridgeRoundtripDemoTests.cpp:15,25,28` skip when the interpreter, pymatgen, or the nanobind module
is unavailable. The skips are correct in isolation — but the suite reports green either way, so a
green run does not tell you the bridge was exercised. Since `Setup.sh`/`Setup.bat` provision exactly
that environment, a skip means the environment is broken, not that the test is inapplicable.

**4. `.clang-tidy` is configured but nothing runs it.** A good check set (`bugprone-*`,
`cppcoreguidelines-*`, `performance-*`, `modernize-*`, plus function-size thresholds at 80
lines/40 statements) with `WarningsAsErrors: ''`, and **no reference to clang-tidy in `scripts/` or
`premake5.lua`.** Baseline verification bought and never collected. Note the function-size threshold
would immediately flag the god-functions found in the architecture and design reviews.

**5. No CI enforces the stated baseline rule.** `docs/work/process/merge-checklist.md` and
`docs/mdbook/test-strategy.md` state "no branch should merge without passing the configured matrix",
but nothing enforces it — the matrix runs when a human remembers. For a solo project this is the
single largest gap between documented process and actual process.

## The single highest-leverage move

**Add `tests/Renderer/Commands/RendererAtomEditCommandsTests.cpp` asserting the undo round-trip
invariant for all twelve commands.**

The oracle is free and fully independent of the implementation: **undo is *defined* as restoring the
prior state.** No knowledge of how the commands work is required to state the expectation —

```
for each edit command:
    original = structure snapshot
    command.Execute(ctx)   → structure differs from original
    command.Undo(ctx)      → structure == original, atoms AND bonds
    command.Redo(ctx)      → structure == post-Execute state
```

**Expected benefit:** closes the only untested path that can silently corrupt scientific data;
resolves the atoms-vs-bonds discrepancy from evidence rather than by reading the code; and gives the
`StructureEditCommand` base-class refactor (design review, finding 1) a safety net *before* the
refactor rather than after.

**Migration scope:** the commands need a `DomainLayer` + `RendererLayer` + `AtomStyleTable` +
`ElementPropertiesTable` and a registered `StructureRecord` — the same fixture
`tests/Renderer/RendererStartupBootstrapTests.cpp` and `tests/Domain/ProjectWorkspaceTests.cpp`
already build between them. No new dependency, no new tooling.

**Smallest safe first step:** one test, one command — `DeleteSelectedAtomsCommand`, which snapshots
both atoms and bonds and is therefore expected to pass. That proves the fixture. Then extend to the
other eleven; the two that snapshot only `atoms` will either fail (a real bug, now caught) or pass
(the optimisation is sound, now documented by a test). Either outcome is worth the test.

**Indicators the change worked:** twelve commands under a stated invariant; the atoms-vs-bonds
question answered by a passing or failing assertion rather than by reading `:483` and `:956`; and the
edit-command refactor performed against a green suite.

## Then, in order

1. **Wire `clang-tidy` into `scripts/python/build.py` or the build matrix** — the configuration is
   already written and reviewed. Start advisory (`WarningsAsErrors: ''` as-is) so it does not block,
   then promote a subset once the backlog is visible. Cheapest gate in this report.
2. **Add minimal CI** (a single workflow: configure → build → run tests on one platform) so the
   documented merge rule is enforced by something other than memory. This is the largest
   process-vs-practice gap; it does not require the full build matrix to be worth having.
3. **Make silent Python skips visible** — have `test_release_dist_tests.py` report skip counts, and
   fail when `ScientificRuntime` skips on a machine where `Setup` claims to have provisioned the
   environment. A skip nobody sees is coverage that does not exist.
4. **Add a `YamlConfigSerializer` round-trip test** — serialize → deserialize → compare, for the four
   config shapes it handles. Same free oracle as finding 1, lower stakes.

## Sound as-is

Say this plainly, because it is unusual:

- **Zero mocks in 247 tests.** Sociable units with real collaborators throughout, verified through
  observable behaviour. This is what the guidance asks for and what most suites of this size do not
  do. Do not introduce a mocking framework.
- **Independent, requirement-shaped oracles everywhere.** No snapshots, no approval tests, no
  recorded outputs, no expectations derived from the implementation. Assertions encode domain math.
  Test names state requirements. This is the highest-risk property of an AI-assisted codebase and it
  is genuinely clean here.
- **Round-trip invariants already used where free** — `FractionalAndCartesianConversionsRoundTrip`,
  `SaveToFileRoundTripsThroughParseYaml`, `CreateEntityAddGetRemoveComponentRoundTrips`. The primary
  recommendation is asking for more of a technique the suite already knows.
- **Integration tests use real resources** — real filesystem, real process spawning, real interpreter
  — rather than mocked drivers. Correct at every one of those seams.
- **`JobSystem` has 8 dedicated test files** (safety, threading, lifecycle, nested submission,
  hierarchy, control, scheduling, resume, events). Proportionate: concurrency defects are expensive
  and untraceable, so buying evidence locally is right.
- **No E2E, and that is the correct answer** — not an omission to apologise for.
- **The suite is pyramid-like**, which fits: rich domain logic and infrastructure, most evidence
  cheap and local. The shape is a description of a justified portfolio, not a target that was aimed
  at.

## Level-by-level verdict

| Level | Verdict |
|---|---|
| Baseline (static analysis) | **Configured, not running.** Fix — cheapest item here. |
| Unit | **Sound.** Good oracles, no mocks, right code selected for testing. One gap: the 12 edit commands. |
| Integration | **Sound and correctly scoped** — narrow, real resources, one boundary at a time. Skips need to be visible. |
| Contract | **Correctly absent** — one deployable, one team. |
| E2E | **Correctly absent** — gate not met. |
| Evaluation | **Correctly absent** — no stochastic surface. |
| Quality practices | Code review and ADRs are strong; **CI enforcement is the gap.** |

The answer to "are we testing right at each level" is **yes at every level except one specific
1,531-line file** — and the level it belongs to is the cheapest one.
