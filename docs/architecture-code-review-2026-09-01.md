# Architecture review — pattern conformance

Repository: `Defect-Studio-mgr`
Date: 2026-09-01
Scope: does the code still follow the architecture the ADRs declare? Where it drifts, what is the
smallest fix?
Baseline: `docs/work/architecture-code-review-2026-07-01.md`

## Method

Read the declared architecture first (`docs/work/architecture/adr/ADR-001..010`,
`docs/adr/0001-state-mutation-policy.md`, `docs/mdbook/architecture-*.md`), then verified each rule
against the current tree — module include matrix, ownership of scientific state, undo paths, and
who mutates what.

## Verdict

**The architecture is the right one for this project, and it is largely intact.** ADR-001's modular
domain monolith is a correct fit — single-user desktop scientific workbench, solo-maintained, the
complexity is domain/rendering/IO, not deployment. Nothing here argues for microservices, plugins,
CQRS, or any data-platform pattern; none of those axes apply.

The load-bearing rule holds where it matters most:

| Module | Depends on |
|---|---|
| `Core` | **Core only** (326/326 internal includes) |
| `Domain` | Core, Domain |
| `Storage` | Core |
| `ScientificRuntime` | Core, Domain |
| `Renderer` | Core, Domain, IO, Events |
| `Presentation` | Core, Domain, IO, Renderer, ScientificRuntime, App, Events |
| `App` | everything (composition root — correct) |

`Core` is a genuine self-contained shared kernel and `Domain` depends on nothing but `Core`. That is
the hard part of a domain-centred monolith and it is real here, not aspirational.

Most of the July findings were actually fixed, not deferred:

- `Renderer` no longer includes `App/` — the P1 leak is gone.
- `ProcessRunOptions` now carries `timeout` and `shouldCancel`; `ProcessRunResult` reports
  `timedOut`/`cancelled`/`terminated` (`src/Core/Platform/ProcessRunner.hpp:13`).
- `ProjectWorkspace` + `StructureRegistry` exist and are populated on the import path
  (`src/App/RendererStartupComposer.cpp:67`, `src/App/RendererRuntimeOpenCoordinator.cpp:95`);
  `RendererStructureData` now carries `domainStructureId` as a derived-snapshot back-reference
  (`src/Renderer/RendererTypes.hpp:55`). The domain is the owner on import — ADR-002's direction is
  now correct.
- `ApplicationBootstrap` shrank from >1000 lines of everything to 1070 lines with the startup
  workflow split into `RendererStartupComposer` and `RendererRuntimeOpenCoordinator`.
- Panels no longer write renderer selection or camera state. The only camera/viewport writes left in
  `Presentation` are in `ExportImagePanel` against its own local preview state, which is fine.

What follows is drift, not rot. Findings are ordered by what will cost most as the app grows.

## Findings

### 1. The module graph has cycles, so "explicit dependency rules" are not actually true

ADR-001 promises "strong internal module boundaries, explicit dependency rules". Three module pairs
are mutually dependent:

- **`Renderer` ↔ `Events`** — `Events/RendererEvents.hpp:5` includes `Renderer/RendererConfig.hpp`
  while `Renderer/RendererLayer.hpp:12` includes `Events/RendererEvents.hpp`.
- **`Renderer` ↔ `IO`** — `IO/AtomStyleIO.hpp:8` includes `Renderer/AtomStyleTable.hpp`,
  `IO/RendererMeshIO.hpp:7` and `IO/RendererStartupLayoutIO.hpp:7` include renderer types; going the
  other way `Renderer/RendererAssetBundle.cpp:5-9` includes five `IO/` headers.
- **`IO` ↔ `App`** — `IO/IOLayer.cpp:15-17` includes `App/Events/ApplicationConfigEvents.hpp`,
  `App/Events/LogExportEvents.hpp`, `App/Serialization/YamlCodecFacade.hpp`. `App` is supposed to be
  the composition root that everything points *at*, not a module that lower layers import from.

**Impact:** there is no acyclic module order, so "which module may depend on which" cannot be stated
or checked. Every cycle is also a recompile amplifier and a reason a module can't be tested or
reused alone.

**Move:** the top-level `Events/` module is the odd one out — it is a bag of cross-module payloads
that drags in `Renderer` and `App` types. Fold module-owned events back into their module
(`Events/RendererEvents.hpp` → `Renderer/Events/`, keeping the event *payload* free of `App` types),
and pull the shared config types (`App/UiConfig.hpp`) down into `Core/Configuration`, which already
exists. For `IO` ↔ `Renderer`: the clean direction is IO parses into its own DTOs and `Renderer`
maps them — but that is a bigger change than the value it returns today; the cheap correct version is
to declare renderer *data* headers (`AtomStyleTable`, `RendererMeshData`, `RendererStartupDefinitions`)
a contract sub-area that IO may depend on, and forbid IO from touching anything else in `Renderer`.

**First step:** `App/UiConfig.hpp` → `Core/Configuration/UiConfig.hpp`. It is included by
`Events/EditorUiEvents.hpp:10`, `Presentation/EditorUiState.hpp:8`, and
`Presentation/ImGuiLayer.hpp:14`, and it is plain config data with no reason to live in the
composition root. That one move removes the `Events → App` edge outright.

**Cost:** a few dozen include rewrites, no behaviour change.

### 2. Nothing enforces the boundaries, so every fix here decays

`premake5.lua` compiles all of `src/**` into one `DefectStudio` project (`premake5.lua:473`). Module
boundaries exist only as convention in ADRs. For a codebase whose stated design pressure is "must
remain understandable after long pauses" and which is edited with AI assistance, convention alone is
the weakest possible enforcement — it is precisely how the three cycles above appeared without anyone
deciding to add them.

**Move:** a small include-direction checker in `scripts/python` — read a declared allow-list of
module → module edges, walk `src/**` includes, exit non-zero on a violation. This is the single
highest-leverage item on the list, because it converts every other finding from "must be
re-reviewed periodically" to "cannot regress".

**First step:** write the checker with the *current* matrix as the baseline (cycles included, marked
as known exceptions), so it passes on day one and only blocks *new* drift. Then remove exceptions as
findings 1 and 3 land.

**Cost:** ~50–80 lines of Python and one call added to the existing build/validation script. No
runtime cost.

### 3. Domain mutation logic lives in `Renderer/Commands`

`Renderer/Commands/RendererAtomEditCommands.cpp` (1531 lines) resolves a domain record and then edits
it in place:

- `domainLayer.Workspace().Structures().FindMutable(*structureId)` (`:87`),
- direct writes to `target->record->structure.atoms` / `.bonds` (`:269`, `:305`, `:384`, `:423`),
- domain rules invoked from the renderer: `ApplyVacancy` (`:280`), `RegenerateAutoBonds` (`:402`,
  `:751`, `:855`),
- undo snapshots of *domain* state (`m_PreviousAtoms`, `m_PreviousBonds`) held in renderer command
  objects.

ADR-002 makes the domain authoritative and ECS/renderer derived. The direction of *ownership* is now
right (finding fixed since July), but the direction of *behaviour* is inverted: the module that owns
the derived representation is the one that knows how to mutate the source of truth.

**Impact:** the second editing surface pays for this. A scripting console, the Python bridge, or
batch defect generation must either duplicate add/delete/vacancy/transform semantics or call into the
renderer to edit a structure. It also means structure-editing rules cannot be unit-tested without a
renderer window.

**Move:** extract the verbs to a domain service — `Domain/Crystal/StructureEditor` with
`AddAtom`, `DeleteAtoms`, `ApplyVacancy`, `TransformAtoms`, each taking a `CrystalStructure&` and
returning a result plus the before/after needed for undo. The renderer commands stay as thin
`ICommand` adapters that resolve the window, call the service, and refresh the derived snapshot.

**First step:** move `ApplyVacancy` and the `RegenerateAutoBonds` call sites — they are already
domain functions being orchestrated from the wrong module, so this is a relocation of ~100 lines with
no new abstraction (consistent with ADR-006: the recurring pattern is now visible, so the abstraction
has earned its place).

**Cost:** one new domain file; renderer commands get shorter. No new indirection for callers.

### 4. Two undo models coexist, and one of them is an empty shell

- `RendererWindowState::viewUndoHistory` / `viewRedoHistory`
  (`src/Renderer/RendererWindowState.hpp:264-265`), pushed and popped by hand in
  `RendererLayer.cpp:732-756`, `:1215-1219`, exposed as the `renderer.undo_view` command
  (`src/Renderer/Commands/RendererCommandRegistration.cpp:533`).
- The global `Core/Undo` stack via `CommandService`.
- `SetCameraViewCommand::Execute` and `::Undo` are still **empty bodies**
  (`src/Renderer/Commands/SetCameraViewCommand.cpp:19-27`), exactly as reported in July.

**Impact:** an `ICommand` implementation that does nothing is worse than either alternative — it
reads as "camera changes are on the global stack" to anyone scanning the command registry, and it
isn't. The first feature that combines a view change with a domain edit in one user action will have
to discover this by hand.

**Move:** decide, and write the decision down. Both answers are defensible:
- *Keep view history local* — camera is arguably local UI state, which `docs/adr/0001` §2 already
  exempts from the command runtime. Then **delete `SetCameraViewCommand`** and add one sentence to
  ADR 0001 saying viewport camera history is deliberately per-window and outside the global stack.
- *Unify* — implement `SetCameraViewCommand` against `CommandService`/`UndoStack` and delete the
  local vectors.

**First step:** the first option. It is a deletion, it matches the behaviour users already have, and
it makes the policy honest. Reopen only if a feature needs camera + domain change to undo atomically.

**Cost:** near zero, and it retires a dead abstraction that has now survived two reviews.

### 5. `RendererWindowState` is a god-struct mixing three ownership classes

522 lines, 133 fields (`src/Renderer/RendererWindowState.hpp`). **53 of those fields are transient UI
interaction state** — `freeLabelDragging`, `sceneArrowGizmoAxis`, `fallbackModalDrag`,
`pinnedMeasurementDragLastMouse`, `addAtomPopupRequested`, and so on. `RendererPanel.cpp` writes
`windowState.*` 187 times, and the top writers are all gizmo/drag/popup fields.

Those writes are *legal* under ADR 0001 §2 (local UI state may stay local). The problem is not who
mutates them — it is that panel-owned interaction state is stored inside the Renderer module's
struct, so the three ownership classes are indistinguishable:

1. renderer runtime state (camera, viewport, visibility toggles),
2. derived domain data (`RendererStructureData structure`, `SceneRegistry sceneRegistry`),
3. UI interaction state (gizmo drags, popups, rubber-band selection).

**Impact:** `GetWindows()` must stay non-const (`src/Renderer/RendererLayer.hpp:98`) purely because
the UI needs to write class 3, which means classes 1 and 2 are wide open to the UI as a side effect.
Any invariant the renderer wants to hold about its own camera or scene cannot be enforced.

**Move:** split the struct into `RendererWindowRuntimeState` (classes 1–2, renderer-owned) and
`RendererWindowUiState` (class 3, panel-owned), then make `GetWindows()` const and hand the UI its own
state through a separate accessor keyed by `windowId`.

**First step:** do the split *inside the same header first* — two nested structs, fields moved, no API
change. That makes the boundary legible and mechanically checkable before it is enforced, and the
diff is a pure field relocation. Moving `RendererWindowUiState` out to `Presentation` and making
`GetWindows()` const is a second, separate commit.

**Cost:** the first step is mechanical. The second touches every `windowState.` reference in
`RendererPanel*.cpp` — worth doing, but only after step one proves the split is right.

### 6. `Storage` is an empty placeholder while `IO` owns project state

`src/Storage` is 43 lines: an empty `StorageLayer` that logs attach/detach
(`src/Storage/StorageLayer.cpp:13`). Meanwhile ADR-004 assigns "project save/load, autosave, session
continuity, references to imported data, manifests and indexes" to Storage — and `IO` currently holds
`ProjectManifestIO`, `ProjectRootsIO`, `RecentProjectsIO`. There is no `SaveProject`/`LoadProject`
anywhere in `src/`, so ADR-008's persistence contract is unimplemented.

**Impact:** low *today* — there is little persistence yet, and ADR-006 explicitly endorses deferring.
But the precedent is already set: three project-scoped units landed in `IO` because that is where the
file access lives, and the next one will follow them. By the time project save/load arrives, "Storage
vs IO" will be a rename of a dozen files instead of a decision.

**Move:** either move the three project-scoped units to `Storage` (they manage project state, not file
formats — format readers/writers correctly stay in `IO`), **or** amend ADR-004 to say Storage
materialises with project save/load and that project-scoped IO lives in `IO` until then. Both are
fine; leaving an empty module named after a responsibility that another module is performing is not.

**First step:** move `ProjectRootsIO` and `RecentProjectsIO` to `Storage/` — session continuity is
Storage's job by ADR-004's own wording, and neither has interesting dependencies.

**Cost:** a file move plus include updates, or a one-paragraph ADR amendment.

### 7. Two ADR directories with colliding numbering

`docs/adr/0001-state-mutation-policy.md` and `docs/work/architecture/adr/ADR-001-modular-domain-monolith.md`
are different decisions that both claim number 1, in two directories, under two naming schemes. The
`docs/adr/` one is also still marked `Status: Draft`.

**Impact:** small but corrosive — ADRs only work if there is one place to look and the numbers are
unique. A reviewer citing "ADR-001" is now ambiguous.

**Move:** consolidate into `docs/work/architecture/adr/` (the larger, indexed set), renumbering the
state-mutation policy to `ADR-011`, and resolve its Draft status — its rules are already being
enforced in review, so it is Accepted in practice.

**First step:** the move and renumber; update the four `docs/mdbook/` references if any point at the
old path.

**Cost:** minutes.

## Leave alone

These are right-sized and should not be touched:

- **The core architectural decision.** Modular domain monolith, one executable, one process. Correct
  for a desktop scientific workbench. No topology change is warranted, and none of microservices,
  event-driven distribution, CQRS, plugins, or any data-platform pattern belongs here.
- **`Core` as a self-contained shared kernel.** 326/326 internal includes. This is the discipline
  everything else rests on — do not relax it.
- **`Domain` depending only on `Core`.** The dependency direction points inward exactly as ADR-001
  and ADR-002 require.
- **The two event lanes** (`EventBus` for subsystem messaging, `Event`/`EventDispatcher` for
  low-level routing). The separation is documented and observed; keep it.
- **The layer stack** as runtime composition, and `Application` as the single composition root.
- **`Core/JobSystem`, `Core/Commands`, `Core/Undo`, `Core/ProgressTrackingSystem`,
  `Core/Diagnostics`.** Real, tested infrastructure with clear ownership. `Result<T>` +
  `StructuredError` over exceptions is applied consistently.
- **`ProcessRunner` cancellation/timeout** and the GIL hook registered by `ScientificRuntime` rather
  than baked into `JobSystem` — both are correct boundary decisions.
- **`App` decomposition into composers** (`RendererStartupComposer`,
  `RendererRuntimeOpenCoordinator`, `Controllers/`). Continue this pattern for the next bootstrap
  concern rather than growing `ApplicationBootstrap` again.
- **Test layout** mirroring the module tree (`tests/Core`, `tests/Domain`, `tests/Renderer/Scene`,
  `tests/ScientificRuntime`, …). Extend it; don't restructure it.
- **`IO/StructureToRenderer` naming** (July's P2). With `Renderer` and `IO` now sharing contract
  types anyway, this is subsumed by finding 1 — fix the boundary rule and the naming question answers
  itself. Not worth a standalone change.

## Suggested order

1. Finding 2 (checker with today's matrix as baseline) — makes everything below stick.
2. Finding 4 (delete the empty command, write the rule down) — minutes, removes a live trap.
3. Finding 7 (consolidate ADRs) — minutes.
4. Finding 1 first step (`UiConfig` → `Core/Configuration`) — removes one cycle edge.
5. Finding 6 first step (project-scoped IO → `Storage`) — sets the precedent before it hardens.
6. Finding 5 first step (split the struct in place) — mechanical, unblocks the const `GetWindows()`.
7. Finding 3 (`StructureEditor`) — the largest, and the one that pays off when the second editing
   surface arrives.

None of these is a rewrite. The architecture does not need to change; it needs its declared rules
made checkable and three modules told to stay on their own side of the line.
