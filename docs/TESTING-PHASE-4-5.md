# Testing: Phase 4-5 (`task/18-phase-4-5`)

Branch: `task/18-phase-4-5`, three commits on top of `main`:

| Commit | What |
|---|---|
| `f79b4fb` | POTCAR export on structure save |
| `054c404` | Supercell Builder panel |
| `5ee9f24` | Materials Collection panel |

## What was verified automatically, and what was not

**Verified:** Release build of `DefectStudio.exe` and `DefectStudioTests.exe`; the full suite at
277 passed / 2 skipped (the two permanent `DS_PYTHON_CAPI_AVAILABLE=0` skips). Two new tests cover
the `pseudopotential_dir` config round trip. `write_potcar.py` was exercised directly against a
synthetic pseudopotential tree for all three paths (success, missing element, missing directory).

**Not verified:** every UI interaction below. **The application was never launched** — starting a
GUI process from the agent environment is blocked, so not even "does it reach the main window" was
confirmed. No panel was clicked, no supercell was generated, no POTCAR was produced by the running
app. The panels compile and are registered, which is not the same as working. Everything under
"Manual checks" is genuinely unchecked, so **start with launching the app**: two newly registered
panels are two new ways for startup to fail.

**Unrelated pre-existing breakage:** `DefectStudioPythonBridge.vcxproj` fails to link
(`__imp_PyComplex_AsCComplex` and three more nanobind symbols unresolved against `python3.lib`).
This surfaced because the generated projects were rebuilt from scratch, not because of this branch;
the `.pyd` had simply been stale-but-present before. It does not affect `DefectStudio.exe` or the
tests, since this build runs every Python bridge through a subprocess. Build the two targets
directly rather than the whole solution:

```
MSBuild build/generated/vs2022/DefectStudio.vcxproj      -p:Configuration=Release -p:Platform=x64 -m
MSBuild build/generated/vs2022/DefectStudioTests.vcxproj -p:Configuration=Release -p:Platform=x64 -m
```

---

## 1. POTCAR export

### Setup

The pseudopotential directory is **not** configured by default, and nothing in the repo can
configure it for you — it points at a licensed VASP distribution. Add it to
`install/users/default/config/ui_settings.yaml` under `ui:`:

```yaml
ui:
  pseudopotential_dir: "C:/path/to/potpaw_PBE"
```

The layout the script expects is one directory per element, each holding a file named `POTCAR`:

```
potpaw_PBE/
  Al/POTCAR
  O/POTCAR
```

An uppercase directory name (`AL/POTCAR`) is tried as a fallback. Anything else — the flat
`Al_POTCAR` style, or the `potpaw_PBE.54` variant suffixes (`Al_GW`, `O_h`) — is **not** handled
and will report the element as missing. If your distribution is laid out that way, say so and the
lookup can be widened.

### Manual checks

- [ ] Open the **New Structure** wizard, build any cell, tick **Export POTCAR**, click **Create**.
- [ ] Focus the new window and press **Ctrl+Alt+S** (`file.save` — note: *not* Ctrl+S, which is
      `project.save`).
- [ ] Two files appear next to each other: `<name>.vasp` and `<name>.potcar`. With a project open
      they land in the project directory; with none, in `install/users/default/`.
- [ ] `<name>.potcar` is the per-element POTCAR files concatenated, in the same alphabetical
      species order the POSCAR uses.
- [ ] Untick the checkbox, create another structure, save it → only `.vasp`, no `.potcar`.
- [ ] Blank out `pseudopotential_dir`, restart, save a structure created with the box ticked →
      the POSCAR is still written, and the log carries
      `POTCAR export requested ... but no pseudopotential directory is configured`.
      **A failed POTCAR must never cost you the POSCAR** — that is the whole point of the split.
- [ ] Point `pseudopotential_dir` at a directory missing one of your elements → POSCAR written,
      log shows `POTCAR not found for element X`.
- [ ] **Open question:** the file is named `<name>.potcar`, mirroring `<name>.vasp`. VASP itself
      wants a bare `POTCAR` sitting next to a bare `POSCAR`. Tell me which convention you want and
      it is a one-line change.

---

## 2. Supercell Builder

Open via **Widok → Supercell Builder**. It acts on the **last focused viewport**, so click a
structure window first; the panel names its source at the top.

### Manual checks

- [ ] With no viewport focused → "No renderer viewport focused..." and nothing else. No crash.
- [ ] Build a cubic cell with one atom (New Structure wizard, a=3.0). Focus it, open the panel.
- [ ] **Simple** tab: set `2 2 2` → "Resulting atom count: 8". **Generate** → a second window
      opens with 8 atoms, and the original unit-cell window stays open beside it.
- [ ] **Matrix** tab: enter rows `(2,1,0) (0,1,0) (0,0,1)` → count reads 2 (determinant 2, one
      basis atom). Generate → 2 atoms.
- [ ] Switch back to **Simple** with that shear still loaded → it refuses to misrepresent the
      matrix as N×M×K and offers **Reset to diagonal** instead.
- [ ] **Matrix** tab: make the determinant 0 (e.g. two identical rows) → Generate is disabled and
      the panel explains why.
- [ ] Make the determinant negative → same treatment.
- [ ] **Surface** tab: hkl `(0,0,1)`, 2 layers, **Suggest** → the button disables and shows
      "Working..." while the ASE subprocess runs. **The UI must stay responsive** — drag the window
      around while it works. On return, the Matrix tab shows the suggested matrix.
- [ ] Focus a *different* structure window → the matrix resets to identity rather than silently
      carrying a matrix meant for the previous cell.

---

## 3. Materials Collection

Open via **Widok → Materials Collection**. Two tabs, two library files:

- **This Project** → `<projectDir>/materials/materials.db` (from `ProjectManifest`)
- **My Library** → `install/users/default/materials/materials.db`

Both are `ase.db` SQLite files; parent directories are created on first use.

### Manual checks

- [ ] With no project open → the **This Project** tab says so. No crash, no stale library.
- [ ] Open a project, focus a structure window, **Save current structure...** → name it, pick
      **This Project**, Save → the row appears in the table.
- [ ] With no project open, the save popup preselects **My Library** and the **This Project** radio
      is disabled — the action stays usable rather than dead.
- [ ] Save with an empty name → the Save button is disabled.
- [ ] Save with no viewport focused → inline error in the popup, nothing written.
- [ ] **Open** on a row → a new window with the same atom count and species.
- [ ] **Delete** on a row → confirm popup. Check that **Cancel is the leftmost button**: a stray
      Enter must not delete.
- [ ] Cancel → row survives. Confirm → row disappears, `materials/materials.db` still on disk.
- [ ] Save something to **My Library**, close the project, open a *different* project → the
      My Library entry is still there, This Project is empty for the new project.
- [ ] The listing is cached deliberately (each call forks a Python subprocess, so listing per frame
      would fork `ase.db` on every repaint). If you change a library outside the app, the table will
      not notice until you hit **Refresh**. Confirm Refresh picks up such a change.

---

## Known gaps

1. **The POTCAR checkbox is never disabled.** The plan called for greying it out when no
   pseudopotential directory is configured. Doing that means giving the wizard panel a live handle
   on `ApplicationConfig`, and panels are constructed *after* `ApplyConfig` runs — so the naive
   wiring reads a config that is not there yet. Instead the checkbox stays live, the hint text next
   to it states the requirement, and an unconfigured export logs a warning rather than failing
   silently. Worth revisiting if it trips you up in practice.

2. **No automated coverage of the two new panels.** They are ImGui render loops against live
   renderer/domain state; there is no panel-test harness in this repo to hang a test on, and
   inventing one for two panels is a bigger change than the panels themselves. Their non-trivial
   logic lives in already-tested code underneath (`BuildSupercell`, `MaterialLibraryIO`).

3. **`SuggestSupercellMatrixJob` is untested.** It is a thin wrapper over
   `SupercellBridge::SuggestSurfaceOrientedMatrix`, which has its own tests; the wrapper only moves
   that call onto a worker thread. The Surface-tab check above is the real test.

4. **Nothing was clicked.** Restating the top of this document because it is the most important
   line in it: the UI paths were compiled and reasoned about, not run.
