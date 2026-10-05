# Operator Redo Panel and Parameterized C_n Arrows Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give every Add operation a Blender-style "adjust last operation" panel, and make the C_2 arrow wrap the bond axis instead of lying in the plane between the two atoms.

**Architecture:** An operator is a named, re-runnable scene operation carrying a data parameter schema. The panel renders widgets from that schema and, on every change, restores a held `before` snapshot and runs the operator again — so the global `UndoStack` gets exactly one entry no matter how many times a slider moves. The C_n arrow becomes the first operator; its hardcoded shape constants become parameters.

**Tech Stack:** C++23, ImGui, premake5 (vs2022 generator, VS 18 MSBuild), GoogleTest.

**Spec:** `docs/superpowers/specs/2026-10-05-operator-redo-panel-design.md`

## Global Constraints

- `Domain` must not depend on UI, renderer or `App`. Operator registry and panel live in `Presentation`; arc geometry in `Renderer/Path`.
- Execution goes through `CommandRegistry` / `CommandService`. Undo goes through the existing `UndoStack` and `SceneObjectsSnapshotCommand`. No new undo API.
- No exceptions in rendering paths.
- New `.cpp` files stay under ~500 lines.
- Regenerate premake projects after adding any new `.cpp`/`.hpp` under `src/` or `tests/`: `scripts/Windows/GenerateProjects.bat`. Premake globs sources at generation time.
- Build and test Release only during development: `scripts/Windows/Build.bat --config Release`.
- `DS_PYTHON_CAPI_AVAILABLE=0`: two GoogleTest cases are permanently skipped. That skip count is expected, not a regression.

## Review Focus

Five conditions the spec implies but does not spell out, each pinned to a test in the task that owns the code:

1. **Degenerate bond** — two atoms at the same position give a zero-length axis. The operation must fail with a `StructuredError`, not emit NaN geometry. (Task 1)
2. **Zero or missing atom radius** — the `r_atom x 1.4` rule must still produce a visible arc, not a zero-radius one collapsed onto the axis. (Task 1)
3. **Sweep at or past 360 degrees** — the arc must not self-overlap into a degenerate closed loop; clamp below a full turn. (Task 1)
4. **Panel outlives its window** — closing the renderer window or tab while the panel is open must close the panel, not restore a snapshot into a dead window. (Task 3)
5. **Selection changes under an open panel** — re-running an operator whose selection no longer exists must close the panel and leave the created objects alone, rather than failing on every frame. (Task 3)

---

### Task 1: Parameterize the curved arrow and add the bond-axis mode

**Files:**
- Create: `src/Renderer/Path/CurvedArrowParameters.hpp`
- Modify: `src/Presentation/Panels/ScenePathCurvedArrow.hpp`
- Modify: `src/Presentation/Panels/ScenePathCurvedArrow.cpp` (the anonymous-namespace `TwoEndAxis` at :41 and `kCurvedArrowFlatness` at :21 are what this task replaces)
- Modify: `src/Presentation/Panels/ViewportAddMenu.cpp:114` (the single call site)
- Test: `tests/Presentation/Panels/SceneCurvedArrowTests.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:

```cpp
namespace DefectStudio
{
    enum class CurvedArrowAxisMode { Auto, Bond, DefectZ };
    enum class CurvedArrowRadiusRule { AtomRelative, BondFraction };

    struct CurvedArrowParameters
    {
        CurvedArrowAxisMode axisMode = CurvedArrowAxisMode::Auto;
        CurvedArrowRadiusRule radiusRule = CurvedArrowRadiusRule::AtomRelative;
        float radiusFactor = 1.4f;      // x largest atom radius, or x bond length
        float sweepDegrees = 270.0f;
        float rotationDegrees = 0.0f;   // offset about the axis
        PathDecorationKind decoration = PathDecorationKind::Arrow;
        glm::vec3 color{1.0f, 0.27f, 0.0f};
        float strokeWidth = 0.04f;
    };
}
```

  and the widened entry point, keeping the old one as a defaulted overload so nothing else has to change:

```cpp
[[nodiscard]] Result<std::vector<SceneObjectId>> AddCurvedArrowThroughSelectedAtoms(
    RendererWindowState &windowState,
    const CurvedArrowParameters &parameters = {});
```

- [ ] **Step 1: Write the failing tests**

In `tests/Presentation/Panels/SceneCurvedArrowTests.cpp`. These four pin the geometry contract and three of the Review Focus items:

```cpp
TEST(SceneCurvedArrowTests, BondModeArcLiesInThePlanePerpendicularToTheBond)
{
    // Two atoms on the x axis. Every node of the produced path must have the same
    // x as the bond midpoint: the arc's plane normal IS the bond direction.
    // Assert the plane explicitly - a node count proves nothing about orientation.
}

TEST(SceneCurvedArrowTests, BondModeRadiusClearsTheLargerSphere)
{
    // radiusFactor 1.4 against the larger of the two atom radii: every node's
    // distance from the bond axis is > that radius, so the arc is outside the sphere.
}

TEST(SceneCurvedArrowTests, DegenerateBondIsRejected)
{
    // Both atoms at the same position: expect a StructuredError, and assert no
    // node holds a non-finite coordinate (the failure must be a rejection, not NaN).
}

TEST(SceneCurvedArrowTests, SweepIsClampedBelowAFullTurn)
{
    // sweepDegrees = 400: the arc must span less than 360 degrees and the first and
    // last node must remain distinct.
}

TEST(SceneCurvedArrowTests, ZeroAtomRadiusStillProducesAVisibleArc)
{
    // Atom radii of 0 with AtomRelative: the radius must fall back to something
    // positive (use the bond-fraction rule as the floor), not collapse onto the axis.
}

TEST(SceneCurvedArrowTests, ThreeAtomCycleIsUnchangedByDefault)
{
    // The existing three-atom expectations, re-run with default parameters, must
    // produce the same axis and the same arrow count as before this change.
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `scripts\Windows\Build.bat --config Release` then the test binary filtered to `SceneCurvedArrowTests.*`
Expected: FAIL — `CurvedArrowParameters` does not exist.

- [ ] **Step 3: Implement**

Add `CurvedArrowParameters.hpp`. In `ScenePathCurvedArrow.cpp`:

- Keep `TwoEndAxis` for `CurvedArrowAxisMode::DefectZ` and for the n >= 3 path — that is today's behaviour and it must not move.
- Add a bond-axis branch: `axis = normalize(b - a)`, origin = `(a + b) * 0.5f`. Reject when `length(b - a)` is below `1.0e-5f` with `ArrowError("curved_arrow.degenerate_bond", ...)`, reusing the existing `ArrowError` helper.
- `CurvedArrowAxisMode::Auto` selects Bond for exactly two ends and DefectZ otherwise, so n >= 3 is unchanged by construction.
- Build the arc as a circle of the resolved radius in the plane through `origin` with normal `axis`, spanning `sweepDegrees` (clamped to `[1.0f, 350.0f]`), started at `rotationDegrees`.
- Resolve the radius: `AtomRelative` gives `max(radiusA, radiusB) * radiusFactor`, falling back to `0.35f * length(b - a)` when that product is below `1.0e-4f`; `BondFraction` gives `length(b - a) * radiusFactor`.
- Delete `kCurvedArrowFlatness` and the sagitta construction it fed once nothing references them.

- [ ] **Step 4: Run the tests to verify they pass**

Expected: all `SceneCurvedArrowTests.*` PASS, and the full Release suite shows no new failures beyond the known skip.

- [ ] **Step 5: Commit**

```bash
git add src/Renderer/Path/CurvedArrowParameters.hpp src/Presentation/Panels/ScenePathCurvedArrow.hpp src/Presentation/Panels/ScenePathCurvedArrow.cpp src/Presentation/Panels/ViewportAddMenu.cpp tests/Presentation/Panels/SceneCurvedArrowTests.cpp
git commit -m "feat: wrap the C_2 arrow around the bond axis and parameterize its shape"
```

---

### Task 2: Operator registry

**Files:**
- Create: `src/Presentation/Operators/SceneOperator.hpp`
- Create: `src/Presentation/Operators/SceneOperatorRegistry.hpp`
- Create: `src/Presentation/Operators/SceneOperatorRegistry.cpp`
- Create: `src/Presentation/Operators/CurvedArrowOperator.cpp`
- Test: `tests/Presentation/Operators/SceneOperatorRegistryTests.cpp`

**Interfaces:**
- Consumes: `CurvedArrowParameters` and the widened `AddCurvedArrowThroughSelectedAtoms` from Task 1.
- Produces:

```cpp
namespace DefectStudio
{
    // One tunable value. The panel renders widgets from these and knows nothing
    // about what the operator builds.
    struct SceneOperatorParameter
    {
        std::string key;
        std::string label;
        enum class Kind { Float, Int, Bool, Enum, Color } kind = Kind::Float;
        float minimum = 0.0f;
        float maximum = 1.0f;
        std::vector<std::string> enumLabels; // Kind::Enum only
    };

    using SceneOperatorValues = std::unordered_map<std::string, std::variant<float, int, bool, glm::vec3>>;

    struct SceneOperator
    {
        std::string id;
        std::string label;             // shown in the panel header and the undo entry
        std::vector<SceneOperatorParameter> schema;
        SceneOperatorValues defaults;
        std::function<Result<std::vector<SceneObjectId>>(RendererWindowState &, const SceneOperatorValues &)> execute;
    };

    class SceneOperatorRegistry
    {
    public:
        [[nodiscard]] Result<void> Register(SceneOperator op);
        [[nodiscard]] const SceneOperator *Find(const std::string &id) const;
        [[nodiscard]] std::vector<std::string> ListIds() const;
    };

    // Registers the C_n arrow operator, id "scene.curved_arrow".
    [[nodiscard]] Result<void> RegisterCurvedArrowOperator(SceneOperatorRegistry &registry);
}
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST(SceneOperatorRegistryTests, RegisterThenFindReturnsTheOperator) { /* ... */ }
TEST(SceneOperatorRegistryTests, RegisteringADuplicateIdIsRejected) { /* StructuredError, first registration kept */ }
TEST(SceneOperatorRegistryTests, CurvedArrowOperatorExposesRadiusSweepAndRotation)
{
    // The registered schema must carry keys "radiusFactor", "sweepDegrees",
    // "rotationDegrees", "axisMode", "radiusRule", "decoration", "color".
}
TEST(SceneOperatorRegistryTests, CurvedArrowDefaultsMatchTheParameterStruct)
{
    // Defaults must equal CurvedArrowParameters{} field by field, so the panel's
    // starting state is the same shape the menu item produces.
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run `scripts\Windows\GenerateProjects.bat` first — these are new files and premake globs at generation time. Then build Release and run filtered to `SceneOperatorRegistryTests.*`.
Expected: FAIL — registry does not exist.

- [ ] **Step 3: Implement**

`SceneOperatorRegistry` is a flat `std::unordered_map<std::string, SceneOperator>`; `Register` rejects a duplicate id with a `StructuredError`. `CurvedArrowOperator.cpp` builds the schema, converts `SceneOperatorValues` into a `CurvedArrowParameters` and calls `AddCurvedArrowThroughSelectedAtoms`. Keep the conversion in one function so the panel never constructs `CurvedArrowParameters` itself.

- [ ] **Step 4: Run the tests to verify they pass**

- [ ] **Step 5: Commit**

```bash
git add src/Presentation/Operators tests/Presentation/Operators
git commit -m "feat: add a scene operator registry with the curved arrow as its first operator"
```

---

### Task 3: Redo panel

**Files:**
- Create: `src/Presentation/Panels/OperatorRedoPanel.hpp`
- Create: `src/Presentation/Panels/OperatorRedoPanel.cpp`
- Modify: `src/Presentation/Panels/ViewportAddMenu.cpp` — the Add entry runs the operator through the panel instead of calling the add function directly
- Test: `tests/Presentation/Panels/OperatorRedoPanelTests.cpp`

**Interfaces:**
- Consumes: `SceneOperatorRegistry`, `SceneOperator`, `SceneOperatorValues` from Task 2.
- Produces:

```cpp
namespace DefectStudio
{
    class OperatorRedoPanel
    {
    public:
        // Captures `before`, pushes ONE SceneObjectsSnapshotCommand, runs the operator,
        // and opens the panel. Records UndoStack::GetUndoDepth() for invalidation.
        [[nodiscard]] Result<void> RunAndOpen(
            const SceneOperator &op, RendererWindowState &window, UndoStack &undoStack);

        // Restores the held `before` into the window and runs the operator again with
        // `values`. Does NOT touch the undo stack.
        [[nodiscard]] Result<void> Reapply(RendererWindowState &window, const SceneOperatorValues &values);

        void Close() noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;

        // Closes the panel when the history moved under it, or the window is gone.
        void PollInvalidation(const UndoStack &undoStack, const RendererWindowState *window) noexcept;
    };
}
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST(OperatorRedoPanelTests, FiveReapplicationsLeaveOneUndoEntry)
{
    // GetUndoDepth() grows by exactly 1 across RunAndOpen + five Reapply calls.
}
TEST(OperatorRedoPanelTests, UndoAfterReapplyRestoresThePreOperationScene)
{
    // One Undo returns the window's scene objects to the state captured before
    // RunAndOpen, not to an intermediate parameter value.
}
TEST(OperatorRedoPanelTests, AnUnrelatedUndoEntryClosesThePanel)
{
    // Push any other undoable edit, then PollInvalidation: IsOpen() is false and
    // that edit survives untouched.
}
TEST(OperatorRedoPanelTests, AClosedWindowClosesThePanel)
{
    // PollInvalidation(stack, nullptr): IsOpen() is false, no restore attempted.
}
TEST(OperatorRedoPanelTests, AFailingReapplyClosesThePanelAndKeepsTheObjects)
{
    // Make execute() return a StructuredError (selection gone). The panel closes and
    // the objects from the last successful run are still present.
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Regenerate projects, build Release, run filtered to `OperatorRedoPanelTests.*`.
Expected: FAIL — panel does not exist.

- [ ] **Step 3: Implement**

`RunAndOpen`: `CaptureSceneObjectsSnapshot(window)` into a held `before`, `undoStack.PushExecuted(CreateSceneObjectsSnapshotCommand(...))` with the operator's label as the description, then `op.execute(window, values)`. Store `undoStack.GetUndoDepth()`.

`Reapply`: `RestoreSceneObjectsSnapshot(window, before)` — pass a copy, the snapshot must survive for the next reapply — then `op.execute`. On a failed execute, close and keep whatever is in the window.

`PollInvalidation`: close when `window == nullptr` or `undoStack.GetUndoDepth() != recordedDepth`.

The ImGui body renders widgets from `op.schema` in the viewport's bottom-left corner and calls `Reapply` when a widget reports a change.

- [ ] **Step 4: Run the tests to verify they pass**

- [ ] **Step 5: Commit**

```bash
git add src/Presentation/Panels/OperatorRedoPanel.hpp src/Presentation/Panels/OperatorRedoPanel.cpp src/Presentation/Panels/ViewportAddMenu.cpp tests/Presentation/Panels/OperatorRedoPanelTests.cpp
git commit -m "feat: add a Blender-style adjust-last-operation panel"
```

---

### Task 4: Axis-constrained modal rotate

**Files:**
- Modify: `src/Renderer/Commands/RendererViewportCommands.hpp`
- Modify: `src/Renderer/Commands/RendererViewportCommands.cpp`
- Modify: `src/Renderer/Commands/RendererCommandRegistration.cpp`
- Test: `tests/Renderer/Commands/RendererViewportCommandsTests.cpp`

**Interfaces:**
- Consumes: the rotation parameter written by Task 1 (`CurvedArrowParameters::rotationDegrees`).
- Produces:

```cpp
// axis: 0/1/2 select the defect frame's x/y/z. Rotates the selected scene objects
// about that axis through the defect origin, as one undo entry.
[[nodiscard]] Unique<ICommand> CreateRendererRotateAboutDefectAxisCommand(
    Ref<EventBus> eventBus, int axis, float degrees);
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST(RendererViewportCommandsTests, RotateAboutDefectAxisTurnsTheSelectedPath)
{
    // A path rotated 90 degrees about the defect z lands where an explicit rotation
    // of each node about that axis puts it.
}
TEST(RendererViewportCommandsTests, RotateAboutDefectAxisIsOneUndoEntry) { /* ... */ }
TEST(RendererViewportCommandsTests, RotateWithNoDefectFrameIsRejected)
{
    // No defect frame: StructuredError, scene unchanged.
}
```

- [ ] **Step 2: Run the tests to verify they fail**

- [ ] **Step 3: Implement**

Follow `CreateRendererAlignAxisCommand` in the same file for the shape of the command and its registration. Rotate every selected scene object's nodes about the chosen defect axis through the defect origin. Bind it into the existing modal R flow so the axis keys pick the defect axes.

- [ ] **Step 4: Run the tests to verify they pass**

- [ ] **Step 5: Commit**

```bash
git add src/Renderer/Commands tests/Renderer/Commands
git commit -m "feat: rotate selected scene objects about a defect axis"
```

---

## Final verification

- [ ] `scripts\Windows\GenerateProjects.bat`
- [ ] `scripts\Windows\Build.bat --config Release`
- [ ] Full Release suite green, with the one known skip and no others.
- [ ] Manual: two atoms > Add > curved arrow gives an arc encircling the bond, clear of both spheres; the panel appears and its radius/sweep/rotation sliders change all arrows at once; `Ctrl+Z` removes the whole thing in one step.
