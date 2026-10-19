# Task 108c: bulk phonon mode at q

Implemented design items 1–8 using the final primitive-cell modulation design. Existing public
session methods and existing test expectations are retained. No computation or animation starts
on project load or case activation.

## Behavior

- Dispersion retains the existing dispersion.tsv layout and adds qpoints.tsv in the exact band-path
  row order, with primitive reciprocal reduced coordinates and the requested summary metadata.
- phonon_mode.py loads/symmetrizes FC as the dispersion script does, computes signed frequencies,
  chooses the closest degenerate partner, and obtains both complex displacement fields from phonopy
  modulation. Each field is independently normalized to a maximum cycle displacement of 1 Å using
  the largest eigenvalue of its two-quadrature Gram matrix. Mass division is not repeated.
- The script finds the smallest commensurate diagonal primitive supercell up to 24. Otherwise it
  computes the capped one-wavelength fragment. Explicit noncommensurate sizes still compute and
  retain the smallest commensurate suggestion when one exists. In-plane forces K = 1. Publication
  reuses the existing transactional publish helper, including relative provenance and preserving
  previous output on failure. POSCAR and vibration.json retain identical atom order.
- New plain domain data, validated IO, dataset recognition and BulkModeField supply the existing
  animator. Mixing combines both quadratures, and participation uses normalized squared complex
  displacement shares. There is no additional C++ commensurability implementation.
- Dispersion selection, bulk-mode selection, q, branch and requested size belong to each case.
  Old global datasets.dispersion migrates silently into cases without a dispersion key; case keys
  win. The existing active-base/case synchronization is retained. Scanned dispersion datasets are
  selected by matching fc_dir, including for inactive cases. Dataset scans retain all cases'
  explicit dispersion and mode references; shared bulks use the shared dataset list.
- Mode requests have one pending slot. Submitting another cancels/detaches the previous request;
  only the newest slot is polled. Completion retains its originating case, following task 109c:
  inactive completion updates that case's selection without replacing the current field. Dispersion
  jobs now retain their originating case too. Project replacement/destruction cancel the mode slot.
- Animacja drgań enables the bulk source when FC is set, supplies q/branch/size inputs, validation,
  progress/cancel, errors, frequency/size readouts, fragment warning and recompute hint, and partner
  mixing. It reuses amplitude, period, phase, arrows and pause/stop. The already-existing animation
  draw call outside the defect-result guard needed no PhononModesPanel.cpp edit.
- Bulk windows use the existing open event and source-dependent supercellPath mechanism, with
  separate case-to-window bindings. Source-path checks prevent binding a defect window just because
  its symbols match. Defect/CCD binding and arrow application reject known bulk windows. Source
  switching stops and clears the old binding; Stop restores the captured float positions exactly.
- Dataset rows mark bulk modes separately, disable spectral comparison for them, and load their
  q/branch/size into the active case. Bulk datasets remain shared rather than exclusive attachments.

## Phonopy basis and physical checks

Runtime: `.venv/Scripts/python.exe`, phonopy **4.2.2**. Inspected installed sources under
`.venv/Lib/site-packages/phonopy/`:

- `api_phonopy.py:2882`, Phonopy.run_modulations: dimension is relative to the primitive cell.
- `phonon/modulation.py:39`: the primitive comes from the dynamical matrix.
- `phonon/modulation.py:58`: the modulation supercell is built from that primitive.
- `phonon/modulation.py:216`: the coefficient is
  `exp(2j*pi*dot(dot(spos, dim.T), q)) / sqrt(m)`.
- `phonon/modulation.py:222`: phonopy additionally divides globally by sqrt(atom count), and its
  subsequent phase choice is global per field. The independent cycle normalization removes these
  global scale factors while retaining the physical per-atom mass dependence and phase convention.

Therefore both q and N×M×K use the primitive basis directly. No unit-cell conversion, hand-built
phase or unit-cell cross-check is used. The lattice conventions also follow the official
[phonopy documentation](https://phonopy.github.io/phonopy/setting-tags.html).

Real hBN input: `test-directory/sjcm_mgr/hBN/bulk/phonons/vasp_661`; symmetrization enabled. Its
phonopy unit cell has 162 atoms, primitive 2 atoms. At K=(1/3,1/3,0), all six branches produce
**3×3×1, 18 atoms, commensurate, no partner** at 0.5 meV tolerance. Frequencies match run_qpoints:

| Branch | Frequency [meV] |
| --- | ---: |
| 0 | 38.73955682481685 |
| 1 | 76.00292012068651 |
| 2 | 110.55207377810025 |
| 3 | 133.7500183849483 |
| 4 | 148.02658290218622 |
| 5 | 159.3239072328395 |

At Γ, the frequencies are approximately [-0.00000427724, -0.00000208905, 0.000000382545,
102.290412193539, 169.428200571913, 169.428200571913] meV. Optical branches **4 and 5 select
each other** as partners; auto gives 1×1×1, 2 atoms. Branch 3 has no partner. Acoustic partners
are selected among the three near-zero branches.

Actual CLI/publication checks also verified K POSCAR symbols and positions against vibration.json
through ASE, independent field normalization, provenance resolution, Γ partner publication,
2×2×1 K fragment plus 3×3×1 suggestion, and preservation of existing output after an invalid band.
A fresh in-plane dispersion with npoints=5 wrote **12 rows in both TSV files**; numerical q rows
match the script's band-path arrays. Scratch output was created outside the repository and removed.

## Files changed

New:

- `scripts/python/examples/phonon_mode.py`
- `src/Domain/Vibrations/BulkMode.hpp`
- `src/IO/PhononModeIO.hpp`, `src/IO/PhononModeIO.cpp`
- `src/ScientificRuntime/Python/PhononModeJob.hpp`, `src/ScientificRuntime/Python/PhononModeJob.cpp`
- `src/Presentation/Panels/PhononPanelSessionBulkMode.cpp`
- `tests/IO/PhononModeIOTests.cpp`
- This report.

Existing:

- `scripts/python/examples/phonon_dispersion.py`
- `src/Domain/Vibrations/VibrationField.hpp`, `src/Domain/Vibrations/VibrationField.cpp`
- `src/IO/AnalysisDatasetIO.hpp`, `src/IO/AnalysisDatasetIO.cpp`
- `src/IO/PhononDispersionIO.hpp`, `src/IO/PhononDispersionIO.cpp`
- `src/IO/PhononPanelSettingsIO.hpp`, `src/IO/PhononPanelSettingsIO.cpp`, `src/IO/PhononPanelSettingsIOCases.cpp`
- `src/Presentation/Panels/PhononPanelSession.hpp`, `src/Presentation/Panels/PhononPanelSession.cpp`
- Session files `PhononPanelSessionCases.cpp`, `PhononPanelSessionDatasets.cpp`,
  `PhononPanelSessionJobs.cpp`, `PhononPanelSessionLifecycle.cpp`, `PhononPanelSessionState.cpp`,
  `PhononPanelSessionWindow.cpp` under `src/Presentation/Panels/`
- `src/Presentation/Panels/PhononModesPanelView.cpp`, `PhononModesPanelDatasets.cpp`, `PhononModesPanelDispersion.cpp`
- `tests/Domain/Vibrations/VibrationFieldTests.cpp`
- `tests/IO/PhononDispersionIOTests.cpp`

The user's task-file edits and unrelated untracked files were preserved. No task edits to renderer,
structure builders, Vendor, install, CCD scripts or phonon_vibronic_analysis.py were made. Generated
project files were produced only through the requested generation command.

## Tests added

Eight GoogleTests, retaining every previous expectation:

- Domain: main/partner quadratures at 0°/90°, equilibrium, participation normalization and weighting,
  absent-partner angle independence and invalid shapes.
- Dispersion IO: absent qpoints, valid finite triples, row count/header/shape/nonfinite failures,
  and failure retaining the previous output.
- Six tests in the new PhononModeIOTests.cpp: geometry/partner loading and scan kind; malformed
  JSON/schema/basis/scalars/geometry/masses/partner; two-case YAML roundtrip and global migration;
  real file loading across cases, distinct dispersion readouts and shared-FC selection without
  autoplay; bulk window isolation, complex animation, mixing and exact Stop/source-switch restore;
  queued request replacement cancellation and project replacement cancellation with no late commit.

Settings/session regressions are grouped in the new allowed IO test file because the existing
settings/views/cases test translation units are already around 500 lines. They exercise the public
session API and real loaders, using the existing Gate/BlockingJob/WaitUntil utilities for cancellation.
No additional production test seam or Python dependency was introduced.

Python --self-test covers (a)–(e), periodic field comparison across a doubled supercell, explicit
fragment suggestions, in-plane closure failure, and malformed sizes.

## Verification, deviations and anything not done

- `.venv/Scripts/python.exe scripts/python/examples/phonon_mode.py --self-test`: passed.
- `.venv/Scripts/python.exe -B -m pytest scripts/python/tests/test_phonon_dispersion.py -q -p no:cacheprovider`:
  **8 passed**. Additional real-data/CLI checks described above passed.
- C++ Tree-sitter grammar checks found no new findings. The session header retains the existing
  constructor default-argument parser limitation. This is not a compilation check.
- `git diff --check` passed. Every modified/new production .cpp is at most 500 lines; existing
  translation units were not reformatted or split. New shared-header members are local blocks.
- Projects regenerated with `$env:DS_TOOLSET='msc-v143'; cmd /c "scripts\Windows\GenerateProjects.bat < nul"`.
  Premake completed, and both projects include the new production files; the tests project includes
  PhononModeIOTests.cpp. The wrapper's submodule step emitted the known sandbox sh.exe
  CreateFileMapping / Win32 error 5. No MSBuild was run.
- Publication directories use `bulk_mode_<uuid>` as direct dataset children of defectstudio rather
  than one replaceable bulk_mode directory. This preserves per-case results and prevents an old
  cancelled subprocess publication from overwriting the newest request. The logical kind remains
  BulkMode and the default-directory helper still uses the bulk_mode kind name.
- For compatibility with the existing invalid-settings test, bulk_q with neither an FC reference
  nor a selected mode reference falls back to mode with a warning. A valid bulk case or a selected
  bulk mode persists bulk_q normally. This also keeps the unavailable source from being restored as
  an actionable source without any inputs.
- Choosing a saved hint with K > 1 clears the global in-plane restriction for that recomputation,
  so the requested hint can actually close periodically.
- Graphify was queried first and gave insufficient current context. Update was not run because
  the explicit task-only file scope excludes graphify-out writes. Context7 was unavailable; installed
  phonopy source and official [Dear ImGui declarations](https://raw.githubusercontent.com/ocornut/imgui/master/imgui.h)
  were the API references. No Vendor API source browsing was needed in this implementation attempt.
- **C++ builds, GoogleTest execution and interactive GUI validation were not run**, as requested.
  Existing-test compatibility and new C++ regressions await the user's build/test run. No requested
  implementation item is deferred. No commit was made.

## Review (Claude, 2026-10-20)

Task file was revised twice during the run (supercell basis = primitive cell; phonopy modulation is
primitive-relative). Verified: Debug tests 1808 (1803 passed, 5 expected skips), Debug app build. GUI not checked.
Follow-up for 108d: `bulk_mode_<uuid>` per request would pile up directories while dragging on the dispersion;
108d switches to a deterministic directory per (q, band, supercell).
