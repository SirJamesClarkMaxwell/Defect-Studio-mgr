# Task 74 implementation report

Implemented on the existing task/70-operator-redo-panel working tree. Changes are uncommitted.
No build, project generation, C++ test execution or live visual verification was performed, as requested.

## Changes and decisions

### Vacancy bonds

- Atom ends keep the 0.9-radius buffer. Atom–vacancy and vacancy–vacancy lines now reach vacancy centres.
- Width is twice the effective bond radius: max(bond.radius, 0.001) times the global bondRadiusMultiplier.
  The existing 0.09 fallback remains when there are no structure bonds. The multiplier is passed from
  RMB, Shift+A, every toolbar Add popup and the vacancy properties button.
- Filled markers use analytic front-sphere depth in the existing isosurface shader. Perspective uses
  a camera ray; orthographic uses the camera normal. The mesh, ring, dashes, colour and opacity remain.
  Solid fill hides the interior line; translucent fill blends over it. Wireframe remains open.
- The ring is drawn last at the same sphere depth with the backend's existing LEQUAL test. The shader
  assigns depth before every return and resets vacancy mode before subsequent orbitals/isosurfaces.
  See the [GLSL depth-output requirement](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html).
- Older generated paths named Vacancy bond also resolve to vacancy centres when saved with buffer 1.0.
  Ordinary vacancy-bound paths retain their buffers. Renamed old paths must be recreated. Saved widths
  remain authored styles; recreate old bonds to adopt the current multiplier.

### Cycle and non-bond arrows

- Added Float controls Promień okręgu (radiusScale, 0.8–2.5, default 1.0) and Odstęp od atomów
  (endGap, 0–3 atom radii). Radial offsets use existing CopyPosition/CopyVacancy bindings.
- Clearance trimming intersects the chord with a sphere around the actual atom, accounting for
  radial offsets. The existing curvature, arc evaluator and short-path non-inversion clamp remain.
- **Default interpretation:** today's GetScenePathAtomBuffer() is 1.15 from the atom centre, giving
  a surface gap of 0.15 radii. The task's literal 1.15 surface-gap default conflicts with preserving
  today's geometry. A clarification was offered; pending a reply, this implementation preserves
  geometry. Operator endGap defaults to max(0, GetScenePathAtomBuffer() - 1), normally 0.15.
  An omitted struct endGap retains the session buffer; an explicit gap uses 1 + gap.
- Scale 1 preserves existing default authored/bound/resolved geometry. Large radial offsets may
  already exceed the requested clearance and then need no extra trim. A smaller gap cannot bring
  the endpoint back to the atom while retaining that radial offset. Short paths retain their
  existing clamp. Clearance is therefore a minimum where the requested geometry permits it.
- These controls are relevant for cycles and non-bond pairs and reapply the entire batch live.

### Bond-axis arrows

- Added Int Liczba strzałek (arrowCount, 1–6, default 2). Default sweep changed from 270° to 150°
  per arrow, capped at 360 / arrowCount - 5 degrees (175° for two arrows).
- Arcs are equally spaced. Common rotation is added to every BondFrame roll. All remain selected,
  follow the bond and share one undo step.
- radiusFactor remains 1.4 times the larger atom radius: it already clears diamond C–C spheres.
  The added clearance test uses a 1.54 Å bond and 0.45 Å atom spheres.
- The existing sweep slider range (1–350°) is retained; creation enforces the per-count cap.
  Direct creation can reach the 355° cap for one arrow. The existing schema expectation is unchanged.

### Add menu

- Removed the developer Path menu and unused composite preset helpers. MakeDevScenePath and
  AddScenePathDecorationGallery remain for existing callers/tests.
- One Krzywa submenu offers Prosta, Krzywa Béziera and Łuk, all round tubes with no arrow decoration.
  Profile editing remains in the N panel. Prosta reuses the free-segment creator.
- Removed the duplicate Linia swobodna entry; the free-arrow entry remains.
- Fixed Polish diacritics in arrow/plane labels and segment, anchoring and orbital tooltips.

## Files changed
- src/Presentation/Operators/CurvedArrowOperator.cpp
- src/Presentation/Panels/RendererPanelContextMenu.cpp
- src/Presentation/Panels/RendererPanelOrbitalMenu.cpp
- src/Presentation/Panels/RendererPanelToolbar.cpp
- src/Presentation/Panels/ScenePathCurvedArrow.cpp
- src/Presentation/Panels/ScenePathCurvedArrow.hpp
- src/Presentation/Panels/ScenePathDevMenu.cpp
- src/Presentation/Panels/ScenePathDevMenu.hpp
- src/Presentation/Panels/ViewportAddMenu.cpp
- src/Presentation/Panels/ViewportAddMenu.hpp
- src/Presentation/Panels/ViewportVacancyAdd.cpp
- src/Presentation/Panels/ViewportVacancyAdd.hpp
- src/Presentation/Panels/ViewportVacancySelection.cpp
- src/Presentation/Panels/ViewportVerticalToolbar.cpp
- src/Renderer/OpenGl/OpenGlVacancyRenderer.cpp
- src/Renderer/OpenGl/Shaders/isosurface.frag
- src/Renderer/Path/CurvedArrowParameters.hpp
- src/Renderer/Path/PathBindingResolver.cpp
- tests/Presentation/Operators/SceneOperatorRegistryTests.cpp
- tests/Presentation/Panels/OperatorRedoPanelTests.cpp
- tests/Presentation/Panels/SceneCurvedArrowTests.cpp
- tests/Presentation/Panels/ViewportVacancyAddTests.cpp
- tests/Presentation/Panels/Task74SceneCurvedArrowTests.cpp (new)

- docs/work/project/tasks/74-arrows-vacancy-bonds-addmenu-report.md (this report)

## Tests and validation

Added/extended:

- Task74SceneCurvedArrowTests: legacy scale-one geometry; outward midpoints; retained bindings;
  both end clearances with radial offsets; counts 1/2/3/6, sweep caps, spacing, rotation,
  selection, bond binding and diamond sphere clearance.
- ViewportVacancyAddTests: centre ends for both bond kinds, unchanged atom buffer, effective radius
  including multiplier/clamp, and legacy compatibility without changing ordinary buffered paths.
- OperatorRedoPanelTests: radius/gap affect every cycle arrow; count/sweep/rotation affect the whole
  bond set; all selected, single undo entry and undo/redo of the complete set.
- SceneOperatorRegistryTests: new parameter types, ranges, defaults and relevance.

Existing default bond counts changed from one to two; vacancy endpoint expectations changed to the
centre. Existing geometry/undo expectations otherwise remain.

Executed successfully with PowerShell: 15 independent math spot-checks for sweep caps, offset-aware
sphere intersections and sphere depth; changed-file encoding/conflict checks; source checks for
operator keys, count clamp, Add menu removal and unconditional shader depth assignment.
Reviewed every Add call site for multiplier propagation and confirmed removed dev helpers have no
remaining callers. Changed C++ files remain under 500 lines.

**C++ tests have not been compiled or run.** These checks do not replace the caller's build/tests.

## Caller follow-up

Premake regeneration is required for one new C++ test source:
tests/Presentation/Panels/Task74SceneCurvedArrowTests.cpp.

Set DS_TOOLSET=msc-v143, run scripts/Windows/GenerateProjects.bat, then build/run DefectStudioTests.
No new application source, dependency, shader file or persistence format was added.

Visually check vacancy bonds at front/oblique/near-axis angles, orthographic/perspective,
Solid/translucent/Wireframe, vacancy–vacancy pairs and a non-unit multiplier. Include ordinary orbitals
after markers. Check C_3 radius/gap and C_2 count/sweep/rotation through redo and both Add menus.

## Knowledge graph

graphify update . was attempted twice (AST-only). The captured retry exited with code 1:
[graphify watch] Rebuild failed: [WinError 5] Odmowa dostępu (access denied).
It also reported an inaccessible pytest cache directory. This matches the prior Task 73 update log.
The graph could not be refreshed in this sandbox and remains unchanged. Captured output:
tmp/task74-graphify.log. Retry the graph update outside the restricted sandbox.