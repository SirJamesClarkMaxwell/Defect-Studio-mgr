# Task 35: scene-object usability, round D

Fifteen items from hands-on testing of `task/30e-scene-shortcuts` -> `task/32b-arrow-paths-and-tips`
-> `task/33-plane-edge-on-visibility` combined (2026-09-18/19). Recorded as reported. Three were
diagnosed and fixed same night, on `task/33-plane-edge-on-visibility`:

- **#1** (giant orange wedge on a Line arrow) - fixed, commit `bc31bc3`. Root cause:
  `ApplySceneArrowKindChange` only re-derived `headWidth`/`headLength` from Arrow2D's pixel scale
  to world units when switching *to* `Arrow3D`, never to `Line`. 32b-1 made `Line` capable of
  drawing a tip too, so a Line inheriting Arrow2D's leftover `22px`/`28px` had those read as
  `22`/`28` *world units* by `SceneArrowGeometry`.
- **#5** (greying out an arrow in the outliner doesn't hide it) - fixed, commit `ad08a66`.
  `SceneArrow::visible` has existed since task 28 (same field `ScenePlane` uses) but
  `OpenGlRendererBackend::renderSceneArrows` never read it, and neither did viewport picking.
- Everything else below is **not triaged** - reported, lightly classified, not investigated
  against the code yet. Do that before estimating or implementing any of it.

## The reports, as given

Numbering matches the original report so it cross-references chat history.

### #2: Arrow/line selection is unreliable

> "są problemy z zaznaczaniem strzałek, lini"

Bug, likely in the viewport. No repro steps captured beyond this. `ViewportSceneArrowInteraction.cpp`'s
`HandleSceneArrowInteraction` picks against `candidate.start()`/`candidate.end()` only - worth
checking whether that degrades for a 32b-1 path with a control point or more than two points (the
hit test may only be sampling the straight chord, not the actual curved/multi-segment shape it now
renders as).

### #3: Orbital orientation and phase

> "jak dodałem orbitale to może powinniśmy dodać możliwość jakiejś łatwiejszej orientacji? aby
> wskazywały w tą samą stronę? oraz zamiany fazy?"

Feature request, not a bug. Two asks: (a) an easy way to align several orbitals' orientation to
match each other/a common axis, (b) a phase (sign) flip. Relates to the orbital work in
`Domain/Electronic/HydrogenicOrbital.*`, `Renderer/Scene/SceneOrbitalGeometry.*` and the 26e/32a UI.

### #4: No right-click context menu on scene objects

> "tutaj może jakieś menu po prawym przyciskiem myszy aby np. usunąć zduplikować, skopiować, takie
> klasyczne rzeczy + skróty klawiaturowe"

Screenshot was the Scene Outliner. Feature request: a context menu (delete/duplicate/copy) on
outliner rows (and/or viewport objects), plus keyboard shortcuts for the same actions. Overlaps
directly with #6 below - probably one piece of work, not two.

### #5: (fixed - see above)

### #6: No Ctrl+C/D/V for arrows, planes, etc.

> "brak ctrl+c/d/v dla strzałek, płaszczyzn itd"

`SceneArrowClipboard` / `GetSceneArrowClipboard()` / `PasteSceneArrowsFromClipboard` already exist
(`tests/Presentation/Panels/SceneArrowOperationsTests.cpp` has clipboard tests) - so arrows may
already have *some* clipboard plumbing that just is not reachable from Ctrl+C/Ctrl+V/Ctrl+D, or
not wired for planes/orbitals/labels at all. Check what exists before building a parallel system.

### #7: Plane scaling can't do one axis at a time

> "jak jest skalowanie płaszczyzn to brak tutaj skalowania tylko jednego boku, tzn, że robimy
> płaszczyznę szerszą albo wyższą"

Feature request: scale a plane's width and height (halfExtents.x / halfExtents.y) independently,
not just uniformly. Gizmo/interaction work, likely `SceneTransform.cpp` (`ApplyTransformDelta`) and
whatever currently drives plane scale.

### #8: A way to mark a vacancy

> "dodajmy jakieś oznaczenie wakansji, tzn aby jakoś zaznaczać na renderze, a dwa aby można było
> zamienić atom na wakansję -> efekt tak jak byśmy usuneli atom z pliku -> w zapisie nie ma go w
> pliku"

Feature request with two parts: (a) a visual vacancy marker in the render, (b) an operation that
turns a selected atom into a vacancy - on save, behaves exactly like the atom was deleted (absent
from the POSCAR/output), but the marker/position is remembered for editing. This is a `Domain`-level
concept (a vacancy is a defect, not a renderer decoration), so it likely needs a real design pass
before implementation - candidate for its own plan doc, not a quick slice.

### #9: Redefinable default/starting view

> "dodał bym możliwość przedefiniowania głównego widoku [...] aby np. dla centrum NV tak to
> wyglądało"

Feature request: let the user set what a fresh viewport frames/orients to by default, with an NV
center framing as the motivating example. Relates to `RendererViewCamera`/view snapshots
(`RestoreViewSnapshot`, session default view already exists per `RendererLayer.cpp` - check whether
this is "expose the existing per-session default view as user-editable" rather than new machinery).

### #10: Orbital rendering artifacts + broken Z rotation

> "atom węgla powinin być podzielony, o ile obórt o x,y działa to o z nie działa"

Two things read off one screenshot: the carbon atom's rendering looks visually wrong/split when an
orbital overlaps it, and rotating the orbital around Z does not work while X/Y rotation does. The Z
axis being special is a strong hint (gimbal-style bug, or an Euler/quaternion convention mismatch
specific to one axis) - check `SceneOrbitalGeometry`/`RotatedEulerDegrees` (already referenced in
memory re: orbital rotation issues from earlier the same day) for anything that treats Z
differently from X/Y.

### #11: A free (unanchored) 3D arrow/line doesn't render

> "jakoś nie mogę dodać swobodnej strzałki3d/lini, znaczy ona się dodała, ale nie była widoczna"

Bug: the object gets created (appears in the outliner, presumably) but nothing draws. Given #5's
root cause (a missing `visible` check) was JUST found in the same render function, check first
whether this is the *same* bug wearing a different hat - e.g. a free arrow created with
`visible = false` by mistake, or landing with `points.size() < 2` and getting silently skipped by
the guard at `OpenGlRendererBackend.cpp` (`arrow.points.size() < 2`), or a zero-length default
placement.

### #12: Changing a tip type does nothing

> "zmiana typów tipa nie działa, wogóle"

Bug, in the 32b-1 tip UI/pipeline specifically. Check whether the properties panel's tip dropdown
actually writes `arrow.startTip`/`endTip`, whether `SceneArrowGeometryHash` (the mesh-cache key)
includes the tip fields - if it does not, a tip change would not invalidate the cached mesh and
would visually appear to do nothing even though the data changed.

### #13: No Bezier-style handle/gizmo for a path's interior points

> "jak dodaję punkt do strzałki to nie pojawia się żaden nowy punkt w środku który mógłbym edytować
> -> brak wąsów znanych z blendera aby móc manipulować przebiegiem krzywej, gizmo, s (x,y,z)
> g(x,y,z), r(x,y,z) oczywiście dodajemy"

This is exactly the scope 32b-1 explicitly deferred: "Editing interior points in the viewport [...]
is its own task; here the list is created straight." Feature request for 32b-2/32c: interior path
points (and the control point) get their own pickable handle, draggable, with G/R/S gizmo support
the way atoms/labels/arrows already have.

### #14: Arrow2D doesn't look good

> (screenshot only, no text)

Visual/aesthetic bug report on `Arrow2D`'s rendering (the SDF quad,
`OpenGlRendererBackend.cpp` around the `Arrow2D` quad/shader code). No specifics captured - get a
clearer description of what looks wrong (proportions? antialiasing? the outline?) before touching
the shader.

### #15: Arrow2D has no gizmo

> "dla strzałki 2D brak gizmo"

Bug/gap: `Arrow2D` cannot be moved/rotated/scaled with the transform gizmo the way `Line`/`Arrow3D`
can. Check `SceneTransform.cpp`'s arrow handling for an `ArrowKind::Arrow2D` exclusion.

## 2026-09-19 follow-up investigation (before dispatch)

Read the actual code for the arrow-shaped items before writing a contract, so Codex spends its
quota implementing instead of re-deriving this:

- **#11 (free arrow/line invisible)**: `AddFreeSegment` (`RendererPanelOrbitalMenu.cpp:97`) seeds
  `points`/`visible` correctly - nothing wrong found. Very likely the *same* bug as #5 (already
  fixed, `ad08a66`: `renderSceneArrows` never read `SceneArrow::visible`). Re-test on the current
  build before spending any more time on this one specifically.
- **#12 (tip type change does nothing)**: NOT a cache-invalidation bug - checked directly.
  `SceneArrowGeometryHash` (`SceneArrowGeometry.cpp:360-361`) already includes `startTip`/`endTip`;
  the render loop (`OpenGlRendererBackend.cpp:2344-2353`) rebuilds the mesh on hash mismatch; the
  Combo widgets (`SceneArrowEditorWidget.cpp:399,406`) write `arrow.startTip`/`endTip` directly
  through a reference into `windowState.sceneArrows[representativeIndex]`, not a copy. Every layer
  checked independently comes back clean, which usually means the repro has a detail not captured
  in the one-line report (multi-selection? a specific tip pairing? Arrow2D, which does not draw
  tips at all per 32b-1's scope?). Get a tighter repro before dispatching this one alone.
- **#15 (Arrow2D has no gizmo)**: `SceneTransform.cpp` itself has no `ArrowKind::Arrow2D`
  exclusion in its translate/scale/rotate paths - so the gap, if real, is upstream: check
  `ViewportGizmo.cpp` / `CaptureSceneTransformSelection` / `RunViewportGizmoChain` for a kind-based
  skip that was never removed, or was deliberate (Billboard orientation makes a 3D rotate/scale
  gizmo ambiguous - if that is the reason, the fix might be "gizmo only in Fixed-plane mode",
  which is a design call, not a pure bug).
- **#2, #14**: not investigated - too little detail in the report to point at code yet.
- **#15 (Arrow2D has no gizmo)**: also checked directly, also comes back clean, same as #12.
  Neither `SceneTransform.cpp`'s `CaptureSceneTransformSelection`/apply path nor
  `ViewportGizmo.cpp` (zero matches for `Arrow2D`/`ArrowKind` in either) branch on arrow kind at
  all - an arrow's snapshot only carries `points`/`controlPoint`/anchors, so the G/R/S gizmo's data
  path treats Arrow2D identically to Line/Arrow3D as far as this reading can tell. If the report is
  accurate, the bug is either purely visual (the gizmo widget renders somewhere wrong/invisible for
  a billboard-oriented Arrow2D specifically) or somewhere not yet checked - not in the transform
  data path itself. Needs a live repro (screenshot with the gizmo toggled on, an Arrow2D selected)
  before spending more time on it.

## Suggested triage order

Quick wins first (small, likely one root cause each): #12 (cache invalidation is a common shape of
bug in this codebase and cheap to check), #11 (may collapse into #5's fix), #15 (may already be 90%
there if `SceneTransform` just needs an `Arrow2D` branch added).

Bigger, needs-a-design-pass items: #8 (vacancy - Domain concept), #13 (path-point editing - was
already flagged as its own follow-up in the 32b task file), #9 (default view - scope depends on
how much "redefine" should mean: one project setting vs. per-structure-type presets like "NV
center").

Everything else (#2, #3, #4, #6, #7, #10, #14) needs a real look at the current code before
estimating.
