# Task 113d: generate a ready job folder for the cluster (embedding + HR + lineshape for big n)

Plan: `docs/work/project/plans/2026-11-17-phonon-server-results-and-comparison.md`. User 2026-11-17: sizes that do not
fit locally -> the app writes a ready job folder, the user runs `sbatch` himself, the app finds the results (113a/113e
rescans). The app NEVER submits jobs and never connects to the cluster.

DECIDED 2026-11-17: the templates are **user assets**, never in the repo. They live in
`install/users/default/scripts/server_templates/` (gitignored; copies of the user's `P:/PL-spectra/scripts`:
`embeding.py, hr_factors.py, lineshape.py, lineshape_sweep.py, run_embeding.job, run.job, run_embeding_series.py`).
The repo contains only the generator; no template text, no copy of the user's scripts, no test fixture copied from
them (tests write their own tiny fake templates).

## Goal

From the active case (defect FC dir, bulk FC dir, GS/ES dirs, HR method/cutoff, σ/σ_max/γ, ZPL) and a size range
n_min..n_max, „Przygotuj zadanie na serwer…” writes a folder (user-chosen, default
`<case source dir>/defectstudio/server_job_<date>` when writable, else under the project) with the six scripts
parametrised (CONFIG blocks filled: SYSTEM paths relative to the job folder on the cluster, sizes, method, cutoffs,
σ/γ/ZPL), `CONTCAR_aligned` files for GS/ES aligned to the bulk lattice (same `origin_shift` as the local script,
104f), and a `README.txt` with the exact commands (`sbatch run_embeding.job …` / `nohup python3
run_embeding_series.py …` / `sbatch run.job hr_factors.py` / `python lineshape.py`). Output layout identical to the
user's (`embedded/`, `spectral-function/`, `lineshape/`) so 113a imports it unchanged.

## Design (binding)

1. Python generator `scripts/python/examples/server_job.py --templates-dir T ...`: copies every file of T into the
   job folder unchanged EXCEPT the block between the lines `# ===== CONFIG =====` and the next `# ==================`
   in `.py` files, which is replaced by generated assignments for the names that block defines (SYSTEM,
   BULK_PHONONS, INIT, FINAL, METHOD, SIZES, CUTOFFS, SIG, SIG_MAX, GAMMA, ZPL, N_MIN, N_MAX, HR_DIR, ... - only
   names already present in that template's block are written; unknown names keep the template's line; a name the
   app has no value for keeps the template's value). Files without the block (`.job`, `run_embeding_series.py`)
   are copied as is, except module-level constants `N_MIN`/`N_MAX` in `run_embeding_series.py` (replace those two
   assignments only). Report which names were set per file in `job.json` in the job folder. Missing templates dir ->
   clear error naming it. Paths on the cluster: the job folder is copied as a whole next to the calculation;
   inputs are referenced relative to it - the README says where to put it.
2. C++ bridge + job like the other scripts; UI section „Serwer” in the case inputs: n_min, n_max (InputInt),
   options, „Przygotuj…” (disabled with reasons: no FC dir / no bulk FC / no GS-ES), result message with the folder
   (relative path, open-in-explorer button).
3. Memory hint in the UI: the user's measured peak RAM (10.6 GB at n=10 to 30 GB at n=50, 36 MPI ranks) as a
   tooltip; no prediction model.

## Files / must-not / acceptance / constraints

- new script + pytest (generated files parse: `python -m py_compile`; CONFIG values present; no absolute Windows
  paths inside the scripts), bridge + job + tests, `PhononModesPanelInputs.cpp` or a new `PhononModesPanelServerJob.cpp`
- must not touch `Vendor/`, energetics, renderer; nothing written outside the chosen folder
- STYLE RULE; regenerate projects; PowerShell built-ins (pytest allowed); no MSBuild; no commit; document deviations
- report `docs/work/project/reports/task-113d-server-job.md`
