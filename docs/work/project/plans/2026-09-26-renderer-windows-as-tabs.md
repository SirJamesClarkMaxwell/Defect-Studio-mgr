# Renderer windows as tabs

Decided 2026-09-26 with the user, arising from "a free window silently loses everything you put
in it". Not scheduled yet - this is the design, written down before it is lost.

## The problem it solves

A renderer panel with no loaded structure ("free window") can hold paths, arrows, orbitals and
planes. They render. They are then discarded on save, silently, because both persistence files
key on the structure's source path:

- `EditorLayer.cpp` scene save loop: `if (window.structureId.is_nil()) continue;` and the group
  key is `SceneObjectsIO::MakeStructureKey(projectDir, record->sourcePath)`.
- `install/users/default/config/project_windows.txt`: each block starts `source=<path>`.

So the persistence model is "scene objects belong to a structure file". A window without one has
nowhere to be written. The user chose to make free windows persist (option B), which needs a
stable per-window identity that survives a session - something the codebase does not have today,
since `windowId` is minted per session.

## The model

Renderer windows become tabs of one outer renderer window, the way browser tabs work.

- **Ctrl+T** opens a new empty tab. An empty tab is a first-class thing, not an error state.
- **Ctrl+W** closes the active tab, offering to save what is in it if it is dirty.
- **`+` on the tab bar** is the same action as Ctrl+T, and lands where a user expects it rather
  than in a panel menu.
- The **toolbars move out of the individual panel and onto the outer window**. A tab receives
  injected content; it does not carry its own chrome. This is what makes `+` fit on the tab bar
  at all, and it removes the per-panel duplication of toolbar state.

A tab's identity is what `scene_objects.yaml` and `project_windows.txt` key on when there is no
structure. With a structure, the structure key can stay as it is.

## What this touches

From a read of the tree on 2026-09-26:

- `src/Presentation/Panels/ViewportToolbars.{cpp,hpp}`, `ViewportVerticalToolbar.cpp`,
  `ViewportTransformToolbar.cpp`, `RendererPanelToolbar.cpp` - currently free functions taking a
  `RendererWindowState &`. Moving them to the outer window means they act on "the active tab's
  state". `RendererLayer::GetFocusedViewportWindowId` and `GetLastFocusedViewportWindowId`
  already exist, so this part is mostly mechanical.
- `src/Presentation/EditorLayer.cpp` - the scene save loop and `saveProjectWindowState`, both of
  which need the new key.
- `src/IO/SceneObjectsIO.hpp` - `PersistedStructureSceneObjects::structureKey` stops meaning
  "structure key" and starts meaning "owner key". Either rename it or add a discriminator; a
  reserved prefix for free tabs is the cheap version, and it is a format change either way.
- `keybindings.yaml` + `RendererCommandRegistration.cpp` - Ctrl+T and Ctrl+W are user actions and
  must go through `CommandRegistry`, per the architecture rules. Ctrl+W in particular must not be
  a raw ImGui key check.
- A per-tab dirty flag, for Ctrl+W's "save first?" prompt. S8 already routes path edits to a
  dirty signal; check whether that generalises before inventing a second one.

## Constraints that are easy to miss

- **`StructureCreationTabsPanel` draws its own viewports** before the structure it is creating
  exists. It must not be forced into the tab model, or it has to be reworked at the same time.
  Check it early; it is the thing most likely to make this bigger than it looks.
- The three-pane creation window is also why "abolish free windows entirely" was rejected.
- Migration: existing `project_windows.txt` and `scene_objects.yaml` files must keep loading.
  Adding an owner-key concept is a format change; decide up front whether it is additive (a new
  optional key, files stay v2) or a version bump.

## Sequencing

Not before the open S11 defects are closed. Those are regressions and quality problems in code
shipped this session; this is a new capability. Doing it first leaves known-broken behaviour in
the tree while a large refactor lands on top of it.

When it starts, it gets its own branch and its own task files, split at least into: the tab
shell and Ctrl+T/Ctrl+W; the toolbar move; the owner-key persistence change. The persistence
half is the one with migration risk and should not share a commit with the UI half.
