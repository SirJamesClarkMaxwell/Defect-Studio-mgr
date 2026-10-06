# Task 30: scene-object remediation

Seven defects and gaps, plus one UI addition, reported after tasks 26e and 29 landed. Ordered by whether the cause is
already known, not by the order they were reported.

## Diagnosed

### 5. Orbitals cannot be selected or deleted

Two separate causes, both confirmed by reading the code rather than guessing:

- **Selection works but is invisible.** `OpenGlRendererBackend::renderSceneOrbitals` is never
  passed `selectedSceneOrbitals`, so a selected orbital draws exactly like an unselected one.
  Every other scene object gets a highlight blend (`kSelectionHighlightColor`, 0.55 mix).
- **Nothing deletes them.** Delete is handled per kind - `ViewportLabelInteraction.cpp:65` for
  pins, `:114` for free labels, and the arrow equivalent - and neither orbitals nor planes have
  an entry. The properties panel's X button is currently the only way to remove one.

Fix: pass the selection into `renderSceneOrbitals` and highlight; add orbitals and planes to the
viewport Delete path; make the outliner rows select them too.

### 4. Object Properties is a wall of every kind at once

`ObjectPropertiesPanel::Render` draws the atom section, the label sections, the arrow list, the
orbital list and the plane list unconditionally. With nothing selected that is five empty
sections; with one arrow selected it is still five sections.

Fix: the panel shows the sections for the kinds actually in the current selection, and a single
"nothing selected" line otherwise. The full per-kind lists move behind a "Wszystkie obiekty"
collapsing header, off by default - they are still the way to reach an object that is off screen.

## To diagnose while fixing

### 1. The gradient does not update the colour

Reported as both "gradient does not work" and "the colour does not update". Those may be one bug
or two. Check, in this order:

- whether the gradient checkbox writes through the "Selected arrows" bulk section, which copies
  `representativeArrow.style` over every other selected arrow *after* the widgets run - a copy
  that would also overwrite a per-arrow edit made in the list above it;
- whether `Line` and `Arrow3D` take the shaft path that was patched, or a second one that was not;
- whether anything caches arrow instances between frames.

### 2. An arrow cannot be added between two atoms

`Add > Rysuj > Strzalka` is gated on `selectedAtomIndices.size() == 2`. Either the selection is
not what that reads, or the menu is reached in a state where it has been cleared. Make the gate
self-describing first - the menu label carries the count it sees ("Rysuj (2 atomy)") and the
disabled items say what they want - so the failing case reports itself instead of needing a
round trip.

### 3. Planes cannot be added

Same gate, same first step. Note that `BeginMenu(label, enabled)` renders a disabled menu that
cannot be opened at all, which is indistinguishable from "the entry is missing" - that alone may
be the whole report.

## New work

### 2b / 6. Matching an arrow to atoms

Two independent operations on an existing `SceneArrow`, not one:

- **Match position**: with two atoms selected, move the arrow's `start` and `end` onto them.
- **Match colour**: take the two atoms' element colours as the arrow's gradient start and finish
  (or as the flat colour, when the arrow has one atom's worth of selection).

Both are one-shot: they read the atoms once and the arrow stays where it was put. That is the
same rule task 29 established and the reason the bond-flavoured design was dropped.

Also a keyboard shortcut to reverse an arrow - swap `start` and `end`, which with a gradient also
reverses the ramp. No obvious conflict with the existing keymap; `X` while an arrow is selected is
the candidate.

### 7. Task 28, unchanged

The eye/camera columns in the outliner, H and Alt+H over every kind of scene object, and the
Blender-style N side panel. Contract and nine failing tests are already committed:
`src/Renderer/Scene/SceneVisibility.hpp`, `tests/Renderer/Scene/SceneVisibilityTests.cpp`.

### 8. Vertical toolbar: add-object buttons

Four new buttons in `DrawViewportVerticalToolbar` (`ViewportVerticalToolbar.cpp`), under the
existing "Add (Shift+A)" button, in exactly this order (one icon each), each opening a small popup
anchored to the button:

1. **Arrow / line** - one icon, popup with "Linia" and "Strzalka".
2. **Plane** - "Plaszczyzna" from the selected atoms.
3. **Atom** - decided: the current "+" button (`plus.png`, "Add (Shift+A)") *becomes* this
   button - it keeps opening the add-atom popup (`AddAtomPopupToggleRequested`) but gets an atom
   icon and an "Add atom" tooltip. There is no separate general "Add" button any more; Shift+A in
   the viewport is unaffected.
4. **Orbital** - the same menu as right-click > Add > Orbital.

Reuse, not copy: the "Rysuj" block (`RendererPanel.cpp:638-686`) is inline in the context menu and
depends only on `windowState`, so it moves into free functions next to `DrawOrbitalAddMenu`
(`RendererPanelOrbitalMenu.cpp`) - e.g. `DrawSegmentAddItems(windowState)` and
`DrawPlaneAddItem(windowState)` - and both the context menu and the toolbar popups call them.
`DrawOrbitalAddMenu` needs a world position and `anchorOrbitalToSelection`, which today live on
`RendererPanel` (`m_ContextMenuWorldPosition`, `m_AnchorOrbitalToSelection`); from the toolbar use
the 3D cursor position and keep the anchor flag on `RendererWindowState` so both callers share it.

Disabled buttons carry the same self-describing tooltip as item 2/3 ("Zaznacz dwa atomy..."), so
the toolbar is also the easiest reproduction of items 2 and 3: if the button is enabled but the
right-click menu is not, right-click is clearing the selection before the menu reads it
(the gate itself - `positions.size() == 2` / `>= 2` - reads correctly).

Icons: `plus.png` is the only add icon in the toolbar set; the four need new PNGs or text fallbacks
(`toolButton` already falls back to a text label).

### 9. Selection-mode buttons (Ctrl+1..5) - decided 2026-09-17

The five selection-mode buttons already live on the vertical toolbar
(`ViewportVerticalToolbar.cpp:201-222`, icons `tool-mode-*.png`). Adding four add-object buttons
makes the toolbar long. User decision:

- **One button.** Collapse the five into one button whose icon shows the current mode; clicking
  it opens a popup listing all five (icon + name + shortcut). Ctrl+1..5 unchanged.
- **New icons.** Redraw the mode icons (and draw the four add-object icons) so they read at
  toolbar size. `install/app/assets/icons/`, same size/style as the existing set; generating them
  with a small Pillow script is fine.
- **Scene objects.** The mask covers atoms, bonds and labels only; arrows, orbitals and planes
  are not named in any mode. Check first how the pick path treats them under each mask (not yet
  read), then decide whether they get a mode of their own or ride on an existing one.

Do this together with item 8, since both change the same toolbar layout.

## Order

1. Item 4 - the panel rework, because every other fix is verified through that panel.
2. Item 5 - selection highlight and delete.
3. Items 1, 2, 3 - the three "does not work" reports, with the self-describing menu first.
4. Items 2b/6 - match position, match colour, reverse.
5. Item 8 - toolbar add buttons (after items 2/3, it reuses their extracted menu code), with the
   item 9 decision made first since both reshape the toolbar.
6. Item 7 - task 28.
