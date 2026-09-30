# Project-scoped free scene persistence

## Purpose

The renderer already permits an empty window in which a user can author paths,
including arrows and bevel settings. That scene is currently transient: the
project save path only gathers objects associated with a loaded structure.

This phase makes one explicit, project-scoped free scene persist with its
project. It is the place for a small, reproducible bevel diagnostic gallery.
The phase deliberately does not change bevel geometry; it makes the existing
behaviour portable, reloadable, and testable first.

## Scope and user workflow

When a project is created or opened, Defect Studio provides exactly one
`Project Scene` renderer window in addition to any structure-backed windows.
The user can create paths and decorations there, press Save, and reopen the
same project to obtain the same scene.

An explicit `--project=<directory>` launch argument opens that project before
consulting the recent-project list. Thus a saved diagnostic project can be
launched reproducibly by a colleague. If the supplied directory is invalid
or cannot be loaded, the application reports the error and continues with the
normal safe startup fallback instead of discarding state or crashing.

The project scene is independent of atom/structure binding. Objects saved in
it retain authored world positions; a binding that cannot be meaningful
without a structure is not silently fabricated. Existing
structure-scoped scenes remain unchanged.

## Persistence model

`scene_objects.yaml` gains an additive optional `projectObjects` section next
to the existing `structures` collection. It uses the existing persisted scene
object representation and the existing extraction/application helpers, so
path style, decoration, bevel options, visibility, and other supported scene
properties use one serialization path.

Missing `projectObjects` means an empty project scene. Existing project files
therefore remain readable without migration. The existing file format version
does not change for this additive, optional field.

The renderer window receives an explicit project-scene identity rather than a
magic nil `StructureId`. This prevents temporary empty or preview windows
from being serialized accidentally. There can be one project-scene window per
open project. Its stable project-derived window identity lets the normal
window-state persistence restore its camera/layout where that subsystem already
supports it.

## Runtime flow

On load, the editor reads structure objects as it does today, creates or finds
the single project-scene window, and applies `projectObjects` to it. On save,
the editor gathers structure-backed objects into `structures` and gathers only
the explicitly identified project-scene window into `projectObjects`.

Edits in the project scene mark the active project dirty even though it lacks
a `StructureId`; save requests therefore cannot silently omit the user’s work.
Closing and reopening a project clears the old project-scene content before
applying the newly opened project’s content, preventing cross-project leakage.

The initial automatic scene is empty by design. The diagnostic arrow gallery
is authored by the user after this phase, so it captures actual current bevel
behaviour rather than a synthetic approximation maintained in code.

## Error handling

Save failures continue through the existing project-save diagnostics and leave
the project dirty. A malformed or absent optional `projectObjects` section is
handled consistently with the current scene-object parser: absent is empty;
invalid content produces a load diagnostic rather than partial, unreported
state. Explicit startup paths are normalized using the existing path helpers
before loading.

## Tests and acceptance criteria

Automated coverage must demonstrate:

1. YAML round-trip preserves `projectObjects`, including a decorated path with
   bevel configuration; a legacy file without it loads as an empty project
   scene.
2. Editor save/load keeps project-scoped and structure-scoped objects separate.
3. Editing the project scene marks the project modified and Ctrl+S persists
   it.
4. Opening a second project does not show free objects belonging to the first.
5. Command-line parsing accepts `--project=<directory>`, gives it precedence
   over recents, and invalid input follows the safe fallback path.
6. A project creates/restores no more than one project-scene window.

Manual acceptance is: create a new project; author arrows with several heads
and bevel values in Project Scene; save; exit; launch with `--project`; and
observe the same objects and usable camera view. That saved project then
becomes the diagnostic artifact for screenshots, automated render capture, and
external bevel review.

## Out of scope and next phases

This phase does not repair general bevel generation, infer expected geometry,
or add a new visual-review browser. After it passes, the next phase is to
author the compact diagnostic gallery, run the existing deterministic GL
artifact tests against it where practical, capture current results, and write
a concise issue report. A subsequent Blender workflow will create reference
images that state the intended bevel geometry separately from the observed
Defect Studio output.
