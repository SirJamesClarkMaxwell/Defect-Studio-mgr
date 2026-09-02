# Dependency Policy

This document defines how third-party dependencies are introduced, stored, patched, updated, and reviewed in DefectsStudio.

The goal is to keep dependency management:
- reproducible,
- understandable after a long break,
- compatible with a modular monolith and one main executable,
- practical for solo development,
- explicit about local modifications and update cost.

## 1. Core principles

- Dependencies are added deliberately, not opportunistically.
- The dependency model must support a clean main repository and predictable builds.
- The project remains a modular monolith with one main executable; dependency choices must not distort that architecture.
- The default question is not “can this be a submodule?” but “what is the lowest-maintenance model that preserves clarity, reproducibility, and updateability?”
- Every dependency must have an explicit source, explicit ownership of local modifications, and an explicit update path.
- Do not add a dependency without a clear reason, a known upstream source, and a compatible license.

## 2. Allowed dependency acquisition models

The project may use the following models:

### A. Forked git submodule
A dependency is included as a git submodule pointing to the project author's fork.

Use this when:
- the dependency is a real upstream project with ongoing life of its own,
- updateability matters,
- local patches are required,
- Premake integration must live with the dependency,
- preserving upstream history is useful,
- pinning to a precise commit is desirable.

### B. Upstream git submodule
A dependency is included directly from upstream.

Use this only when:
- no local modifications are needed,
- no project-specific Premake integration is needed inside the dependency,
- the team wants a clean direct relationship with upstream.

### C. Vendored snapshot in `Vendor/`
A dependency is copied into the repository as a fixed snapshot.

Use this when:
- the dependency is small,
- the dependency is stable,
- updates are expected to be rare,
- preserving full upstream history inside the main repo is not worth the overhead,
- submodule management would cost more than it saves.

### D. Single-header or small drop-in dependency
A tiny dependency is stored directly in `Vendor/` as one file or a very small set of files.

Use this when:
- the dependency is truly small,
- the integration is simple,
- local control is more valuable than update tracking overhead.

### E. System package / package manager dependency
This model may be used for local tooling or optional developer setup, but it should not become the primary source of truth for core project dependencies unless there is a strong reason.

Use carefully when:
- the dependency is a build tool rather than a product dependency,
- local environment convenience matters more than repository vendoring,
- reproducibility is still acceptable.

## 3. Default selection rule

Choose the lightest model that still gives:
- reproducible builds,
- understandable update history,
- explicit local patch ownership,
- low maintenance overhead.

### Practical default

- Use a **forked submodule** for medium or large dependencies that are expected to evolve and that need project-specific integration.
- Use a **vendored snapshot** for small, stable, or rarely updated dependencies.
- Use a **single-header drop-in** only for genuinely tiny dependencies.
- Avoid relying on system packages for core libraries required to build the application itself.

## 4. Criteria for choosing submodule vs snapshot

A dependency should usually be a **forked submodule** if several of the following are true:
- it is a real standalone repository,
- it is medium or large,
- preserving upstream history is useful,
- updates are expected,
- local patches are required,
- Premake files are required inside the dependency,
- pinning a commit is important,
- update discipline matters more than repository simplicity.

A dependency should usually be a **vendored snapshot** if several of the following are true:
- it is small,
- it is header-only or near-header-only,
- it is stable,
- updates are rare,
- no meaningful local divergence is expected,
- submodule management would create more friction than value.

## 5. Forked submodule policy

When a dependency is stored as a forked submodule, the following rules apply:

- The submodule points to the project author's fork.
- The fork must keep a clearly documented relationship to upstream.
- The upstream repository URL must be recorded.
- The submodule must be pinned to an exact commit, not a floating branch.
- Local modifications should be minimal and intentional.
- Premake integration may live in the fork if that is the chosen integration model.
- The fork must not become a dumping ground for unrelated project-specific hacks.
- If the dependency can be updated from upstream, that path should remain understandable.

### Required documentation for each forked submodule

For each forked submodule, record:
- dependency name,
- upstream repository,
- fork repository,
- pinned commit or tag,
- why submodule was chosen,
- what local modifications exist,
- whether Premake was added or modified,
- how updates from upstream should be handled.

## 6. Vendored snapshot policy

When a dependency is stored as a vendored snapshot:

- The dependency must live under `Vendor/<Name>/`.
- The copied version must be pinned to a known upstream release, tag, or commit.
- A small metadata note must record:
  - upstream source,
  - version or commit,
  - date of import,
  - local modifications, if any.
- License files and attribution must be preserved where required.
- If the snapshot is modified locally, that fact must be explicit.

Vendored code must not silently drift away from its known origin.

## 7. Premake integration policy

Premake integration must follow the same clarity rules as the dependency itself.

- If a dependency is a forked submodule and always consumed with Premake in this project, keeping Premake files in the fork is acceptable.
- If a dependency does not need a custom fork, avoid creating one only to host a tiny build tweak unless that tweak is expected to be maintained long-term.
- Build integration should be understandable from the repository layout and docs.
- The main repository should not contain hidden dependency-specific build magic that cannot be traced back to the dependency record.

## 8. Update policy

Dependency updates must be explicit and reviewable.

- Updating a dependency should normally be a dedicated commit.
- Large dependency updates should normally not be mixed with feature work.
- Each update should record:
  - what changed,
  - why it changed,
  - whether local patches had to be rebased or adjusted,
  - what risk areas exist.
- Do not update dependencies “while already touching the file anyway.”
- If an upstream sync is difficult, that is a signal that the fork is diverging too much.

## 9. Local patch policy

Local dependency patches must be treated as real maintenance cost.

- Patch only when necessary.
- Prefer minimal, well-scoped changes.
- Every patch should be explainable.
- If the project carries a patch long-term, the reason must remain documented.
- If a patch becomes large, reconsider whether the dependency should remain external in the same form.

## 10. License and provenance policy

No dependency may be added without checking:
- license compatibility,
- source authenticity,
- whether attribution files must be preserved.

For every dependency, the origin must remain traceable.

## 11. Dependency registry

The project should maintain a simple dependency registry, either in this file or in a separate companion document.

Minimum fields:
- Name
- Acquisition model
- Upstream URL
- Fork URL (if any)
- Version / tag / commit
- Local modifications
- Premake integration status
- Update notes

## 12. Decision checklist

Before adding a new dependency, answer:

1. Why is this dependency needed?
2. Is it core to the product build, or only a development convenience?
3. Is it likely to be updated?
4. Will we patch it locally?
5. Do we need our own Premake integration inside it?
6. Is it large enough that preserving upstream history matters?
7. Is submodule overhead justified?
8. Is a vendored snapshot simpler and safer?
9. What is the license?
10. How will this dependency be updated six months from now?

## 13. Practical default for DefectsStudio

Until there is a strong reason otherwise:

- Large or important third-party libraries that are likely to be updated and that need custom integration should be stored as **forked submodules**.
- Small, stable, or low-churn dependencies should be stored as **vendored snapshots** inside `Vendor/`.
- Every dependency must have an explicit rationale and traceable provenance.
- Dependency management should optimize for clarity and long-term maintainability, not only for convenience today.
