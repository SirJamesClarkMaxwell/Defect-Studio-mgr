# Task 69: UI feedback round 2 (Add menu, multi-orbital aim, plane alignment, outliner scope)

Branch `task/65-feedback-round`. Another codex agent (task 68, paths) and the caller edit other files
in the same working tree at the same time: touch only what this task needs and list every file you
change. The user's screenshot of the orbital "Skieruj na" combo is attached.

## 1. One Add menu everywhere

Today "what can be added" is spread over several menus that list different things in different
orders: the RMB context menu "Add" submenu (`src/Presentation/Panels/RendererPanelContextMenu.cpp`),
the Shift+A popup (`RendererPanelToolbar.cpp`, "Blender-style Shift+A"), the vertical-toolbar draw /
orbital menus (`RendererPanelOrbitalMenu.cpp`), `DrawDefectAddItems` (`ViewportVacancyAdd.cpp`),
`DrawDefectFrameAddMenu` (`ViewportDefectFrame.cpp`), and any main-menu-bar Add entry. Make ONE
function, e.g. `DrawSceneAddMenu(RendererWindowState&, ...)` in a new
`src/Presentation/Panels/ViewportAddMenu.{hpp,cpp}`, and call it from every place (each place may
keep a short header, but the item list, grouping, order, names and enable/disable rules must be the
same). Suggested grouping (Polish labels like the rest of the UI, with the English word where the
existing UI uses one):
- Atom... (the existing add-atom popup)
- Defekt: Wakans (Vacancy), Wiązania wakansów, Etykiety wakansów, Osie defektu (empty) >
- Rysuj: Linia, Strzałka (through 2 selected atoms/vacancies), Zakrzywiona strzałka (C_n) - call
  `AddCurvedArrowThroughSelectedAtoms(RendererWindowState&)` from `ScenePathOperations.hpp`, which
  task 68 is adding right now (signature `Result<SceneObjectId>
  AddCurvedArrowThroughSelectedAtoms(RendererWindowState &)`; if it is not there yet when you look,
  declare nothing, just call it - the caller builds after both tasks), free path/arrow, płaszczyzna
  (plane), orbital(e) >, tekst (free label), pomiary/etykiety as they exist today.
Every item shows its shortcut when it has one and a tooltip when disabled saying what to select.
Do not lose any action that exists today in any of those menus.

## 2. Aim many orbitals at once

The single-orbital N panel (`ObjectPropertiesPanelOrbital.cpp`, `DrawAim`) has "Skieruj na" (combo of
`CollectOrbitalAimTargets`) + "Skieruj" + "Ustaw w osiach defektu". With several orbitals selected
(`DrawSelectedSceneOrbitalSection` in `ObjectPropertiesPanelSections.cpp`) only "Wyrownaj orientacje"
exists. Add the same combo + "Skieruj wszystkie" (each selected single-centre orbital that has an
axis aims from ITS OWN centre at the chosen target, one undo step) and "Ustaw wszystkie w osiach
defektu". Typical use: three p orbitals on the carbons around a vacancy all pointing at the vacancy.
Also a target "Każdy na najbliższy wakans" would be useful (each orbital aims at the vacancy nearest
to its centre) - add it if cheap. Unit-test the multi-aim helper (pure function in
`Renderer/Scene/SceneOrbitalAim.{hpp,cpp}`).

## 3. Align a plane's own axis to a defect axis

User: "can I align a plane to the defect axes so that the plane's y axis runs along z?" Check what
"Wyrównaj zaznaczone do osi" (`ViewportDefectFrame.cpp`) and the plane N panel do today. Add, for
planes (and anything else that has a rotation and own axes: orbitals, paths), a way to choose the
mapping: which own axis (x, y, z / normal) goes along which defect axis (x, y, z), keeping a sensible
second axis (e.g. own x onto defect x, or the projection of the current x). A small submenu
"Wyrównaj oś obiektu..." with entries like "y obiektu wzdłuż z defektu", or two combos + button in the
plane N panel - your choice, one undo step. Unit-test the rotation math (pure function in
`Renderer/Scene`).

## 4. Scene Outliner shows only the displayed scene

User: "maybe the scene outliner should show only things from the scene currently displayed?" Check
`src/Presentation/Panels/SceneOutliner*`: if it lists objects of other windows / structures / the
project scene, restrict it to the window (structure scene) that currently has focus / is shown in the
active viewport, with the header naming that scene. If it already does that and lists something else
(e.g. stale rows), find and fix that instead. Explain what you found.

## Constraints

Same as task 60 (`docs/work/project/tasks/60-tex-text.md`, Constraints). You cannot build (the
caller builds and sends errors back). Do not touch `install/users/**`, `Vendor/**`, `src/Domain/**`,
`src/Renderer/Path/**`, `src/Renderer/OpenGl/**`, `src/Presentation/Panels/ViewportPathInsert.*`,
`src/Presentation/Panels/ScenePathOperations.*`, `src/Presentation/Panels/ScenePathEditCommands.*`
(task 68), `src/Presentation/EditorLayer*`, `src/IO/**` (caller). In `ViewportVacancyAdd.cpp` and
`ViewportDefectFrame.cpp` only move/rename the menu-drawing functions; the caller edits the bond
logic in `ViewportVacancyAdd.cpp` (`AddVacancyBonds`) right now. Report: files changed, tests added,
what you found about the outliner, decisions.
