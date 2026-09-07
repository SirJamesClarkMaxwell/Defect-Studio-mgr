# Structure Lifecycle Contracts

Canonical API surface and directory conventions for the structure-lifecycle-unification plan (12-step implementation 2026-09-07+).

These contracts are binding — steps 2–12 implement precisely this, not a variation.

## Project Structure Directory Convention

A structure directory inside a project is identified by the presence of **`POSCAR`** at its root.

- **`POSCAR`** — canonical structure file (always present, canonical source of truth). Read/write via `PoscarWriter`/Python bridge.
- **`CONTCAR`** — reserved for relaxed/optimized structures, results of external computation. Read-only from DefectStudio's perspective; may exist alongside `POSCAR` but does not define the directory.
- **`<name>.vasp`** — legacy single-file format (deprecated in this unification; see Step 11). Not used for new structures added to projects.

Example project directory tree:
```
MyProject/
├── project.defectstudio
├── structures/
│   ├── Si_bulk_2x2x2/
│   │   └── POSCAR
│   ├── Al_defect/
│   │   ├── POSCAR
│   │   └── CONTCAR
│   └── GaAs_interface/
│       └── POSCAR
```

A directory without `POSCAR` is not a structure directory, even if it contains other files (e.g., analysis results, metadata, or a stray `CONTCAR`).

## Lifecycle Events

Events published by `EventBus` during structure add-to-project workflow. Defined in `src/Core/EventBus.hpp` or equivalent.

### `ProjectTreeSelectionChanged`

Published whenever `ProjectTreePanel` user selection changes (file/directory/project root clicked).

```cpp
struct ProjectTreeSelectionChanged : EventBus::Event {
  std::filesystem::path selectedPath;  // Raw filesystem path of the selected item
  enum Kind { File, Directory, ProjectRoot } kind;  // What type of item is selected
  std::filesystem::path resolvedTargetDirectory;  // If kind==Directory, this directory; if kind==File, parent directory; if kind==ProjectRoot, empty (no valid target yet)
  
  // Invariant: if resolvedTargetDirectory is non-empty, it is known to exist and be inside a registered project root
};
```

**Preconditions:** None. Fired on every selection change.

**Invariants:** Fired only from main thread. Never fired with `selectedPath` pointing to a non-existent location.

---

### `AddStructureToProjectRequested`

Published by Structure Hub panel when user clicks "Add to project" button.

```cpp
struct AddStructureToProjectRequested : EventBus::Event {
  Domain::CrystalStructure structure;  // The structure to add (lattice, basis, bonds, all validated)
  std::string displayName;  // Human-readable name for the structure directory
  std::filesystem::path targetDirectory;  // Parent directory where <displayName>/ will be created
  
  // Invariant: targetDirectory exists and is known to be inside a registered project root
};
```

**Preconditions:** `targetDirectory` must exist and be validated as inside a registered project root. `displayName` must be non-empty and sanitized for filesystem use (no `/`, `\`, `:`, `*`, etc.).

**Invariants:** Fired only from main thread. Never fired with invalid/nonexistent paths or empty names.

---

### `ProjectStructureAdded`

Published by app-layer `StructureLifecycleCoordinator` after successful add-to-project write and domain registration.

```cpp
struct ProjectStructureAdded : EventBus::Event {
  Domain::StructureId newStructureId;  // Unique ID assigned by domain registry
  std::filesystem::path poscarPath;  // Full path to written POSCAR file
  
  // Invariant: the structure is already registered in Domain and visible to queries
};
```

**Preconditions:** POSCAR write succeeded, domain registration succeeded.

**Invariants:** Fired only from main thread. By the time this event is published, `Domain::FindStructureById(newStructureId)` is guaranteed to succeed and return the registered structure.

---

### `ProjectStructureAddFailed`

Published by `StructureLifecycleCoordinator` if add-to-project job fails or domain registration fails.

```cpp
struct ProjectStructureAddFailed : EventBus::Event {
  Domain::StructuredError error;  // Machine-readable error with category, message, suggestion
  // Original user intent preserved so Structure Hub can offer retry without losing the draft
};
```

**Preconditions:** POSCAR write failed, or domain registration failed, or target validation failed.

**Invariants:** Fired only from main thread. Domain registry is NOT modified; the structure is NOT added. Structure Hub remains in its current state, allowing the user to edit and retry.

---

## Lifecycle Commands

Commands handled by `CommandRegistry` (defined in `src/Core/CommandRegistry.hpp` or equivalent). Keybindings wired in `install/users/default/config/keybindings.yaml`.

### No new keybindings required for Step 1

Lifecycle operations are not driven by keybindings; they are driven by user actions in the Structure Hub UI (button clicks, mode selections). Structure Hub listens for `ProjectTreeSelectionChanged` events and publishes `AddStructureToProjectRequested` on user intent.

Future steps may add keybindings for "Create supercell from selected structure" or "Export to file", but these are out of scope for Step 1.

---

## Boundary Invariants

1. **Project Tree isolation:** `ProjectTreePanel` never resolves project semantics. It publishes raw selection; the app-layer coordinator (`StructureLifecycleCoordinator`) validates that the selected path belongs to a known project root.

2. **IO contract:** Structure write happens via a `JobSystem` job (never synchronously in a `Render()` method). The job returns either a path or a `StructuredError`; no silent failures.

3. **Domain contract:** `StructureRecord.sourcePath` is set only via the Step-2 domain operation (`RegisterStructureInProject` or similar). No ad hoc `.sourcePath =` assignments anywhere else.

4. **Renderer contract:** Renderer windows open by `StructureId` only, never by `CrystalStructure` value. The id must already exist in the domain registry.

---

## Naming Rationale

- **ProjectTreeSelectionChanged** (not "ItemSelected", not "PathClicked"): Emphasizes that the event carries a resolved directory, not just which widget was clicked.
- **AddStructureToProjectRequested** (not "SaveStructure", not "CreateStructure"): Mirrors the UI button label and clarifies intent (add to the specific project, not export/save-to-library).
- **ProjectStructureAdded** (not "StructureSaved", not "StructureCreated"): Emphasizes the domain-layer operation (registered and assigned an id), not the I/O operation (file written).
- **ProjectStructureAddFailed** (not "SaveFailed"): Matches the success event name, clarifies it's a project-level operation failure, not a generic IO failure.
