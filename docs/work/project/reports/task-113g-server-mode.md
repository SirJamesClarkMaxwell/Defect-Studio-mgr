# Task 113g — server mode vector on demand

Implemented 2026-11-17. No commit, MSBuild invocation, or C++ test execution.

## Behaviour

The server dataset can fetch one selected mode through **Wczytaj wektor modu**, enabling arrows, or
starting animation. The existing JobSystem runs `phonon_mode_vector.py`; the session commits the
reader's result on the main thread. A pending animation starts only for the same selected mode,
dataset, case, and bound window. Stop, hiding the view, or losing the window clears that intent.
Mode, case, dataset, and project replacement cancel obsolete requests.

`server.hr_dir` in the dataset summary resolves to `<system>/embedded/<AxBxC>`. Portable
`inputs_relative.hr_dir` takes precedence when recorded. **Otwórz superkomórkę** sends that directory's
POSCAR through the existing window-opening event and binding flow; it does not require FC inputs.
Missing `modes.h5` or POSCAR disables fetching with the reason in the tooltip.

The session retains eight `(dataset, mode)` entries in LRU order. Fetched rows are shared with the
active result and removed from the existing plot cache on eviction. Reopening a dataset restores
its retained rows; refreshing its summary invalidates them. Dense local result arrays remain on
their existing path. Server rows use a sparse map, including cells above 2000 atoms. The mode cache
therefore holds at most `8 * N * 4` floats, rather than allocating a quadratic matrix.

## HDF5 layout and convention check

Inspected the specified P: file read-only and inspected the installed dephonopy
`Modes.from_hdf5`, `Modes.save_data_to_dir`, `ModesBase._calculate_displacements_hdf5`, and
`HDF5Eigenpairs` implementations. The real file contains:

- `evecs`: `(864, 864)`, mode-major mass-weighted rows;
- `freq_mev`, `freq`, `evals`, `iprs`, `irreps`: `(864,)`;
- geometry datasets for 288 atoms.

The app's index K maps directly to HDF5 row K, including translations 0–2. The script performs only
`evecs[K, :]` and `freq_mev[K]`, reads POSCAR with ASE, and validates masses, shapes, finite values,
index bounds, and nonzero norm. No full eigenvector or frequency array is read. h5py documents
these integer/slice selections as [HDF5 hyperslabs](https://docs.h5py.org/en/stable/high/dataset.html).

The stored row is already mass weighted. Following the local script's convention means converting
to physical displacement with `1/sqrt(m)`, multiplying by `sqrt(m)`, and normalizing over all atoms
and Cartesian components. The intermediate physical normalization in dephonopy cancels in the
final normalization. Output is little-endian float32 `(N, 3)`; participation is `sum_xyz(e**2)`.
The unequal-mass fixture compares this with the full local conversion and checks participation
sums to one for every mode, including translations.

Real-data read-only smoke check: **A1 mode 863, 199.81642253370856 meV**, vector shape **(288, 3)**,
float32 participation sum **1.0**. This verifies the reader, not the GUI animation.

## Implementation choices and deviations

- Sparse rows are used for all server datasets, rather than creating dense arrays below 2000
  atoms. `hasEigenvectors` keeps its existing meaning of a complete dense result; the shared
  `ModeRows` accessor exposes fetched rows to arrows, animation, participation, and VibrationField.
- The job returns an output directory, matching `PhononModeJob`. The session calls the IO reader;
  ScientificRuntime does not depend on IO. The strict existing NPY parser was moved into
  `PhononModeVectorIO` and reused by `PhononAnalysisIO`, avoiding a second parser.
- `mode.json` additionally records the explicit normalization convention and source provenance.
  Output publication reuses the existing transactional `publish()` implementation. Temporary
  outputs live under the project's `defectstudio/phonons/.mode-vectors`, are removed after polling,
  and have job-lifetime cleanup for abandoned requests. Pre-existing output directories are refused.
- Degenerate mixing is offered only when the partner's row is also available; selecting/fetching
  one mode never implicitly reads its partner.
- The old server-session test's task-113g placeholder-text assertion was updated: geometry can
  bind before a vector is fetched, while all unavailable vector consumers still return empty data.

## Validation

- New Python suite: **5 passed**.
- New mode-vector plus existing server-import suites: **13 passed, 7 subtests passed**.
- Requested full pytest command: **120 passed, 5 failed**. Those failures encountered denied access
  to the existing tests' default Windows temp directories; pytest also could not write `.pytest_cache`.
- Full suite repeated with a process-local `tempfile.mkdtemp` override using UUID directories inside
  `.task-113g-temp/pytest` with inherited permissions, TMP/TEMP redirected there, and pytest's cache
  disabled: **124 passed, 1 failed, 21 subtests passed**. The remaining failure is unrelated:
  `test_check_include_directions.py::TestCheckIncludeDirections::test_disallowed_edge_detected`
  executes `assertIn("Domain", violations[0])`, but `violations[0]` is a `Violation` object and is not
  iterable. No test/checker files outside this task were changed to conceal this failure.
- Twelve C++ tests added: four reader tests; two accessor/domain tests, including a 2001-atom sparse
  result; four session tests for LRU eviction across datasets/plot cache, refresh invalidation,
  window binding/animation restoration, and request cancellation/animation intent; two job tests
  for cancellation and preservation of pre-existing output. **Written but not built or run.**
- Include-direction checking: **zero violations in task implementation files**. The repository-wide
  checker reports three existing violations in untouched `Events/ProjectEvents.hpp` and
  `Core/Domain/StructureLifecycleEvents.hpp`.
- `git diff --check`: passed. Modified implementation `.cpp` files remain below 500 lines.
- Ran the required PowerShell generation command with `DS_TOOLSET=msc-v143`. Exit code 0;
  both generated application/test projects include the new sources/tests. The script emitted a
  Git-shell `CreateFileMapping ... Win32 error 5` and existing Premake deprecation warnings, but
  Premake completed generation. No hand edits to generated projects.
- Ran `graphify update .`: failed with `[WinError 5]` / access denied. Graph refresh remains pending.

## Manual click path

1. Open the project and select the imported dimer **12×12×1** server dataset in **Fonony**.
2. In **Animacja drgań**, choose **mod defektu**, then select **A1 mode 863 (~199.82 meV)**.
3. Press **Wczytaj wektor modu**. After completion, its participation and arrow counts are available.
4. Press **Otwórz superkomórkę**. It should open the server embedded POSCAR with **288 atoms**.
5. Press **▶ Animuj**; inspect arrows and vibration, then **⏹ Stop** and verify exact restoration.
   Alternatively, open the window first and press Animuj to fetch and start automatically.
6. Select other modes with arrows enabled to fetch them on demand; revisit retained modes to use
   the cache. Visit more than eight distinct dataset/mode pairs to exercise eviction.

The user still needs to build/run C++ tests and perform this GUI check.

## Files changed by this task

- `scripts/python/examples/phonon_mode_vector.py` (new)
- `scripts/python/tests/test_phonon_mode_vector.py` (new)
- `src/IO/PhononModeVectorIO.{hpp,cpp}` (new)
- `src/IO/PhononAnalysisIO.cpp`
- `src/ScientificRuntime/Python/PhononModeVectorJob.{hpp,cpp}` (new)
- `src/Domain/Vibrations/PhononModes.{hpp,cpp}`
- `src/Domain/Vibrations/VibrationField.cpp`
- `src/Presentation/Panels/PhononPanelSessionModeVector.cpp` (new)
- `src/Presentation/Panels/PhononPanelSession.{hpp,cpp}`
- `src/Presentation/Panels/PhononPanelSessionCases.cpp`
- `src/Presentation/Panels/PhononPanelSessionJobs.cpp`
- `src/Presentation/Panels/PhononPanelSessionLifecycle.cpp`
- `src/Presentation/Panels/PhononPanelSessionState.cpp`
- `src/Presentation/Panels/PhononPanelSessionWindow.cpp`
- `src/Presentation/Panels/PhononModesPanelView.cpp`
- `tests/IO/PhononModeVectorIOTests.cpp` (new)
- `tests/ScientificRuntime/PhononModeVectorJobTests.cpp` (new)
- `tests/Presentation/Panels/PhononModeVectorSessionTests.cpp` (new)
- `tests/Domain/PhononModesTests.cpp`
- `tests/Presentation/Panels/PhononServerSessionTests.cpp`
- This report.

Task 113c's convergence/Origin files and its changes to the shared panel header were left intact.
No renderer internals, energetics, Vendor, or install files were edited by this task. P: was read only.
