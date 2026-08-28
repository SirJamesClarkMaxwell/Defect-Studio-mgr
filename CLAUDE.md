# DefectStudio

VESTA-clone crystal-structure/defect visualization tool, C++23 + premake5 (VS2022 generator,
actually built by VS "18"/2026's MSBuild - see below) + Python subprocess bridges for scientific
computing (scipy, pymatgen, punktukas-tools).

This file exists because `AGENTS.md` at the repo root carries the load-bearing project rules
(Ponytail mode, reuse-first checklist, graphify) but is NOT auto-loaded by Claude Code - only
`CLAUDE.md` is. Read `AGENTS.md` in full; the rest of this file is workflow rules from
`docs/work/project/TODO.md` ("Zasady pracy" / "Granice architektoniczne") that aren't restated
there.

## Zasady pracy (docs/work/project/TODO.md)

- Jeden branch per task: `task/NN-short-name`
- Merge do main tylko po pełnym Debug + Release build (both DefectStudio.exe AND
  DefectStudioTests.exe, tests green in both configs) - see the `full-build-verify` skill.
- Pliki `.cpp` max ~500 linii - dzielić na moduły.
- Brak wyjątków w ścieżkach renderowania - granica udokumentowana.
- AI-generated code podlega aktywnemu review przed mergem.

## Granice architektoniczne (obowiązujące, nie do naruszenia bez decyzji)

- `Domain` nie zależy od UI, renderera ani `App`.
- `Renderer` może czytać typy domenowe przy budowie snapshotu, ale **nie jest** źródłem prawdy domenowej.
- `IO` czyta/zapisuje pliki; nie zawiera logiki transformującej domenę do konkretnego widoku
  (most `CrystalStructure -> RendererStructureData` żyje w `Renderer/StructureRendererDataBuilder`, nie w `IO`).
- `Presentation` renderuje UI i zbiera intencje użytkownika; nie mutuje po cichu stanu innych modułów.
- `App` składa systemy (composition root); nie ma być orkiestratorem logiki domenowej.
- Komunikacja: cross-layer przez `EventBus`; akcje użytkownika przez `CommandRegistry`+`CommandService`+keymap;
  długie operacje przez `JobSystem`+`ProgressTracker`; błędy jako `StructuredError`.
- Tylko main thread commituje stan widoczny w projekcie/UI.
- Nie mieszać pojęć: struktura domenowa / scena renderera / kolekcja UI / projekt na dysku.

## Build

Premake generator is `vs2022` (`scripts/Windows/GenerateProjects.bat`), but the actual installed
toolchain on this machine is VS "18" (2026) - MSBuild lives at
`C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`, not the
`...\2022\...` path the name suggests. Regenerate projects after adding any new `.cpp`/`.hpp`
under `src/` or `tests/` - premake globs sources at generation time, not build time (a `.claude`
hook reminds about this after `Write` creates a new source file). Use the `full-build-verify`
skill for the full pre-merge sequence.

`DS_PYTHON_CAPI_AVAILABLE=0` in this build (both configs, `premake5.lua:457,623`) - embedded
Python is off, every Python bridge (`ScriptRunner`) goes through a subprocess, cold `import`
included. Two GoogleTest cases are permanently skipped because of this
(`ConPtyProcessTests.RunsCommandAndProducesOutput`,
`BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable`) - that skip count is
expected, not a regression.

## Structure loading: punktukas-tools vs pymatgen

New structure loads go through `PuntukasBridge` (wraps `punktukas-tools`, an ASE-`Atoms`-based
local package at `~/punktukas-tools`), not `PymatgenBridge` - established preference, see memory.
`punktukas-tools` has a generic periodic-MIC distance helper
(`puntukas.atoms.base.AtomsBase.get_distances`, backed by phonopy's `find_mic`) but **no**
structure-comparison/atom-matching utility (no `linear_sum_assignment`, no `StructureMatcher`
equivalent) - `Domain/Crystal/StructureComparison.{hpp,cpp}` +
`ScientificRuntime/Python/ScipyAssignmentBridge` is original code, not a wrapper around an
existing punktukas feature.
