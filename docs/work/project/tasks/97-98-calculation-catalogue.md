# Task 97-98: calculation catalogue (OUTCAR head/tail, grouping, proposal) and energetics.yaml

## Goal
The energetics module can turn a folder of VASP runs into (defect, q) groups with a proposed
converged run each, read cheaply over a slow network mount, and keep the scan plus the user's
choices in the project file `energetics.yaml`. No UI in this task.
Plan: `docs/work/project/plans/2026-10-11-energetics-auto-and-mu.md`.

## Files to create or change
- `src/Domain/Defects/CalculationCatalogue.cpp` (new) - everything declared in `CalculationCatalogue.hpp`
- `src/IO/VaspRunRecordIO.cpp` (new) - everything declared in `VaspRunRecordIO.hpp`
- `src/IO/EnergeticsIO.cpp` (new) - everything declared in `EnergeticsIO.hpp`

## Files that must NOT be touched
- the three headers above and `tests/Domain/Defects/CalculationCatalogueTests.cpp`,
  `tests/IO/VaspRunRecordIOTests.cpp`, `tests/IO/EnergeticsIOTests.cpp` (the contract)
- everything else in the repo (in particular Presentation, Renderer, App, the FNV files)

## Acceptance criteria
1. `DefectStudioTests.exe --gtest_filter=CalculationCatalogueTests.*:VaspRunRecordIOTests.*:EnergeticsIOTests.*` passes (Release x64).
2. The full suite still passes (`--gtest_brief=1`).
3. `CalculationCatalogue.cpp` includes only std, Core and Domain headers.

## Constraints and hints
- ChargeFromFolderName: same rules as `ChargeFromCalculationFolder` in
  `src/Presentation/Panels/FnvCorrectionInputs.cpp` (copy the regex; Presentation will call the
  Domain one later - do not edit Presentation now).
- MethodMismatches / reader: store PREC and PRECFOCK lower case (VASP prints "Normal" in one build
  and "normal" in another; they must compare equal). Compare doubles with 1e-6.
- ProposeEnergyRun reason strings in Polish, short (e.g. "zbieżne HSE, PRECFOCK normal, zgodne z
  bulkiem"; "wybierz: 2 równorzędne"; "brak zakończonego obliczenia").
- GroupDefectRuns: paths are compared component-wise (Path, not string prefix), case-sensitive.
- VaspRunRecordIO::Read: open OUTCAR in binary, read the first kHeadBytes and the last kTailBytes
  with a seek (never the middle); size and write time via std::filesystem (write time as
  seconds since epoch of file_time_type converted with clock_cast or a duration_cast - any
  monotonic stable integer is fine, it is only a cache key).
- FindRuns: std::filesystem::recursive_directory_iterator with skip_permission_denied, no symlink
  following, errors via std::error_code (no exceptions escaping).
- ParseEigenval: line 1 has ISPIN as its 4th number, line 6 has NELECT NKPTS NBANDS; each k-point
  block = blank line, k-point line, NBANDS lines "index e [e_dn] occ [occ_dn]".
- EnergeticsIO: yaml-cpp like FnvCorrectionsIO.cpp (same relative-path and temp-file+rename
  pattern; reuse its helpers if they are reachable, otherwise mirror them). Doubles with full
  precision (max_digits10). Runs serialised as a list of maps; empty optional fields omitted.
- Use PowerShell built-ins, not Git Bash tools. No git commands that change state.
- After creating the .cpp files run `cmd /c "set DS_TOOLSET=msc-v143&& scripts\Windows\GenerateProjects.bat"`.
- Build: `& "D:\Aplications\VisualStudio\VisualStudioIDE\MSBuild\Current\Bin\MSBuild.exe" build/generated/vs2022/DefectStudioTests.vcxproj /p:Configuration=Release /p:Platform=x64 /m:4 /nr:false /v:minimal`
- Skip the graphify update for this task (it is done once after merge).
- Each `.cpp` under ~500 lines.
