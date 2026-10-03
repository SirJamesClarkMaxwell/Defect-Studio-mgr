# Defect electronic structure: SALC to densities

Status: planned 2026-10-03. Follows `vacancy-and-defect-orbitals.md` (tasks 51-53). Only task 54
has a full task file (`docs/work/project/tasks/54-salc-basis.md`). 55-58 are specified here at plan
level, and each gets its own task file when it is next.

The chain this plan builds:

    dangling bonds -> SALC -> Hamiltonian eigenorbitals -> Slater determinants -> multiplets
                   -> 1-RDM -> rho(r), rho_s(r)

The program stays a tool for *seeing and understanding* defect physics, not a quantum-chemistry
package. Nothing below adds physics only to make a picture look better.

## Verification of the task-53 SALC code (2026-10-03)

Checked numerically against groupy on the dev machine, not assumed. The probe scripts are not in
the repo. The checks are reproduced as the task 54 Python tests.

The test applied is the partner property. Every copy `k` of an irrep Γ must satisfy
`D(g) V_k = V_k Γ(g)` for all `g`, where `V_k` has the copy's rows as columns. It is the defining
property of a symmetry-adapted set and the one tasks 57 and 58 rely on.

### Finding 1 - multiplicity > 1 rows are not consistent copies (confirmed)

`groupy_point_group_analysis.py` (and the spike `groupy_reduce_representation.py`) project each
row `r` separately with `P_rr`, run Gram-Schmidt on that row's image, and number the results
`occurrenceIndex = 0, 1, ...` independently per row. The image of `P_rr` is the whole
m_Γ-dimensional row-r subspace. Gram-Schmidt picks an arbitrary orthonormal basis of it,
determined by `rref` pivot order. Nothing ties the k-th vector of row 0 to the k-th vector of
row 1.

| Case | Result of the current script |
|---|---|
| C3v, 6-site general orbit (E, m=2) | neither E copy passes the partner test; the two rows of a "copy" do not even span an invariant subspace |
| Td, 24-site orbit (E m=2, T1 m=3, T2 m=3) | every copy of E, T1 and T2 fails the same way |
| m = 1 (NV: A1 m=2 but 1-D; E m=1) | correct, because the row subspace is 1-D and only a sign is free |

So the NV- results shown so far are right, but any defect with a repeated degenerate irrep gets
wrong "copies". Example: a vacancy with a second neighbour shell in the basis.

### Finding 2 - the permutation matrices are transposed (new)

`build_representation` writes `matrix[source, target] = 1`. With groupy's convention, where a
function on site s goes to site g·s, the correct entry is `matrix[target, source] = 1`. As written,
D(g) is the matrix of g⁻¹: D(gh) = D(h)D(g), an anti-homomorphism (verified for C3v and Td).

- Characters are traces, so the decomposition and the reducible characters are unaffected.
- Real irreps with real orthogonal matrices are unaffected, because their P_rr is invariant under
  g -> g⁻¹. That is why NV looked right.
- **Complex irreps get swapped labels.** On a C3 triangle the vector the script labels E+
  transforms as E- under the correct D. Every C3, C4, S4, C6, C3h, S6, C4h, T and Th result
  printed so far has its E+/E- labels swapped.
- Transfer operators P_r0, which the fix for finding 1 needs, are wrong under the transposed
  matrices for non-abelian groups.

### Finding 3 - irrep matrices for real-type irreps (latent)

The script uses `get_repr()`. For some real-type irreps of the cubic groups (T2 of O, T2g/T2u of
Oh), groupy's default matrices are complex |2,m> combinations, so the SALC coefficients come out
complex for an irrep that has a real basis. groupy's own `ActiveSpace` switches those irreps to
`get_repr(real=True)`. The SALC code must use the same matrices, so that SALC row r and
active-space orbital row r are the same partner function (task 58 needs this).

### The fix (validated on C3v m=1..3, Td, D3d, C3, C4h, S4, C6, C3h, S6, T, Th)

1. D(g) with `matrix[target, source] = 1`. The result is verified to be a homomorphism.
2. Irrep matrices follow groupy's `ActiveSpace` rule: `get_repr()`, but `get_repr(real=True)` when
   the Frobenius-Schur indicator is +1 and the default matrices are complex.
3. Seeds come from `P_00`: `rref` pivots, then Gram-Schmidt (Hermitian), which gives m_Γ
   orthonormal seeds v_0^(k).
4. Partners: v_r^(k) = P_r0 v_0^(k), using `generalized_projection_operator(rep, r, 0)`. Because the
   irrep matrices are unitary and the permutation rep is orthogonal, P_r0 is an isometry from row 0
   to row r. Normalisation and orthogonality across copies are therefore automatic.

All copies of all irreps pass the partner test, the full set is orthonormal, and the dimensions
add up to the site count. Cost is under 0.5 s for 24 sites.

## Task order and dependencies

```
54 SALC basis + complex irreps ──┬──> 57 multiplicity mixing / model Hamiltonian ──> 58 1-RDM densities
                                 │                                                     ^
55 vacancy semantics + PBC ──────┴──> 56 live orbital aim                              │
                                       (independent of 57/58) ─────────────────────────┘ (58 reuses 56's live LCAO)
```

54 comes first because 57 and 58 both assume that `(irrep, occurrence)` names one consistent copy
and that complex irreps have real partners. 55 and 56 are independent of 54 and could run in
parallel with it.

---

## 54 - Correct SALC basis + real basis of complex irreps

Status: implemented 2026-10-03 on `task/54-salc-basis`. Python acceptance (11), task gtest filter
(74) and the Release full suite are green (only the known 5 bevel failures + 1 skip). Debug build and
the manual NV / NV-as-C3 drawing check are still open.

Full spec: `docs/work/project/tasks/54-salc-basis.md`.

- **Goal.** Projected vectors are true symmetry-adapted copies (findings 1-3 fixed). Complex
  irreps (E+/E- of C3, C4, S4, C6, C3h, C4h, C6h, S6, T, Th) get a drawable real basis
  u = √2 Re z, v = √2 Im z. It is produced in Python and drawn with the existing LCAO path.
- **Physics.** Only consistent copies make a multiplicity-space operator such as h_Γ ⊗ I meaningful
  (task 57). For a complex pair, z and z* are degenerate under time reversal. Their real
  combinations span the same 2-D space and are real functions, which an isosurface can draw.
- **Architecture.** Python does all algebra in a new shared module,
  `scripts/python/examples/salc_basis.py`, used by both scripts. JSON keeps the flat
  `projectedVectors` list with a documented invariant and adds `realPairVectors`. C++ validates
  the invariant at the bridge boundary, stores the result, shows it and draws it. No symbolic
  algebra in C++.
- **Decision: no nested `irrep -> occurrence -> row` type.** Migrating to one would touch about 7
  consumers: table, export, active-space selection, recompute identity, draw, and 2 test files.
  The invariant gives the same guarantee for the cost of one validator: every
  (irrep, occurrence) has rows 0..d-1, emitted in irrep -> occurrence -> row order. Grouping is a
  lookup, not a type.
- **Out of scope.** Mixing copies (57), export of the real basis in the Markdown/LaTeX table
  (follow-up), double groups and quaternionic irreps (they do not occur for single crystallographic
  point groups; they are refused explicitly), and orbital (p/d) bases with non-permutation
  representations.

## 55 - Vacancy semantics + PBC

- **Goal.** Deleting an atom no longer declares a defect. "Utwórz wakans" is an explicit action.
  A vacancy's position has a recorded provenance and is computed correctly across cell
  boundaries. The marker has independent viewport and export visibility.
- **Physics / why.** A vacancy is a claim about the crystal, not a side effect of editing. The site
  of a relaxed vacancy is best defined by the unrelaxed reference lattice site. The centroid of
  relaxed neighbours is a fallback, and in PBC it must be computed on unwrapped positions.
- **Architecture.**
  - Domain: split `ApplyVacancy` into `RemoveAtoms(structure, indices)` (erase, reindex and bond
    cleanup, no record) and `ApplyVacancy` (`RemoveAtoms` plus a `VacancySite` record).
    `BuildDefectedStructure` keeps `ApplyVacancy`, because there a vacancy is the declared defect.
  - `VacancySite` gains `VacancyPositionSource source {RemovedAtom, ReferenceStructure,
    NeighborCentroid, Manual}`. It is persisted in `PersistedVacancy` as an optional key, absent =
    `Manual` for old files.
  - New domain helper `PeriodicCentroid(structure, atomIndices)`. It unwraps by minimum image
    around the first atom (`MinimumImageCartesianDelta`, PeriodicGeometry.hpp), averages, and wraps
    back into the cell. `GroupTheoryPanel::selectionBasis`'s centroid already does the unwrap
    implicitly through `BuildSelectionBasis`. Both callers, that one and RendererPanel "Dodaj
    wakans" (currently a plain mean, marked ponytail there), use the one helper.
  - Reference source: from a `StructureComparisonResult`, each `unmatchedReferenceAtomIndices`
    entry is a vacancy candidate at the reference atom's position, mapped into the defect cell.
    Menu: "Wakanse z porównania ze strukturą referencyjną".
  - Commands: the existing delete becomes plain removal. New "renderer.atoms.delete_as_vacancy"
    ("Usuń jako wakans") records a vacancy per removed atom. Undo is already covered: the delete
    snapshot restores `vacancies` since task 51.
  - Visibility: `RendererVacancyData` gets `visible`/`renderable`, held the way `RendererAtomData`
    holds them across rebuilds (check `BuildRendererStructureData`). The outliner rows get the
    standard two columns (`SceneOutlinerVisibilityColumns`). `showVacancies` stays as the group eye.
    The export pass skips `!renderable`.
- **Tests.** RemoveAtoms leaves `vacancies` alone. ApplyVacancy records `RemovedAtom`. A centroid
  across a cell face equals the wrapped true centroid; the naive mean would be about half a cell
  off. Reference-derived vacancies have the right position and source. YAML round-trip includes
  source and flags, and old files load as Manual and visible. Undo of both delete flavours.
- **Acceptance.** Plain delete adds no marker. "Usuń jako wakans" adds one at the exact old position.
  A boundary-straddling selection gives the right centroid. A marker hidden for export is absent
  from the PNG but visible in the viewport, and vice versa.
- **Out of scope.** Antisite/interstitial markers. Automatic vacancy detection without a reference
  structure.

## 56 - Live orbital aim

- **Goal.** An orbital (single-centre or an LCAO component) can carry a persisted aim binding and
  stays pointed at its target while atoms move (relaxation playback, edits).
- **Why.** After relaxation R_i(t) moves, but the one-shot `rotationEuler` from task 52 does not.
- **Architecture.**
  - `OrbitalAimBinding {TargetKind kind (Atom, Vacancy, Cursor, FixedPoint, AtomCentroid);
    targetAtoms (indices, PersistedAtomRef on save); vacancyIndex; glm::vec3 fallbackPosition;
    glm::vec3 referenceUp; float twistDegrees}`. It is held as an optional on `SceneOrbital` and on
    `LcaoComponent`.
  - Orientation is a deterministic function, never an increment:
    `n = normalize(target - centre)`, `e2 = normalize(referenceUp - (referenceUp·n) n)` (with a
    fixed fallback axis when they are parallel), `e3 = n × e2`. The frame [e2, e3, n] maps
    the member axis onto n. The twist is a rotation about n. `referenceUp` is captured once at
    bind time from the current orientation, so binding does not visibly move the orbital at
    twist 0. No `current orientation -> minimal rotation` per frame, so there is no history
    dependence and no accumulated roll.
  - `ResolveAnchoredOrbitals` (already called every frame) writes the resolved euler into
    `rotationEuler`. `MakeSceneOrbitalMeshKey` already hashes `rotationEuler`, so the mesh rebuilds
    only when the direction actually changes. No extra invalidation logic is needed.
  - SALC builder (task 53): components get an `AtomCentroid` or `Vacancy` binding for the analysis
    centre, so a drawn SALC keeps its lobes pointing inward during playback.
  - Stale target: use `fallbackPosition`. A vacancy index is resolved, then the fallback is used,
    and on load the vacancy is rebound by nearest position.
- **Tests.** Same inputs give bit-identical euler regardless of call history. Moving the target
  rotates the lobe and moving it back restores it exactly. Twist turns about the lobe axis only.
  Binding at twist 0 reproduces the pre-binding orientation. Antiparallel and
  parallel-to-reference cases are finite. Persistence round-trip including atom rebinding after
  reorder.
- **Acceptance.** NV dangling-bond lobes bound to the vacancy stay aimed through a relaxation
  trajectory. A drawn SALC follows too.
- **Out of scope.** Aim constraints for two-centre presets. Animation smoothing.

## 57 - Repeated irreps: manual and Hamiltonian mixing

- **Goal.** For m_Γ > 1, turn the arbitrary SALC copies into physical eigenorbitals. Two modes are
  labelled differently everywhere: "Mieszanie ręczne" (manual) and "Model hamiltonianu"
  (Hamiltonian model).
- **Physics.** H commutes with G, so on V_Γ = M_Γ ⊗ R_Γ it acts as h_Γ ⊗ I_d, with h_Γ of size
  m×m. Group theory fixes the block structure, not h_Γ. One U_Γ diagonalises h_Γ and is applied
  identically to every row (e_x and e_y mix the same way). That only works because of task 54's
  consistent copies.
- **Architecture.**
  - Manual (m = 2): θ slider, ψ = R(θ) φ, applied per row. For m > 2 there are no Euler sliders;
    use the Hamiltonian mode.
  - Hamiltonian model, parametrised in the *site* basis rather than by typing h_Γ. Python
    additionally returns the orbits of site pairs under G: on-site classes, e.g. {N}, {C1,C2,C3},
    and hopping classes, e.g. {N–C}, {C–C}. That is cheap there, because the permutations already
    exist. The UI asks one number per class (ε_N, ε_C, β_NC, β_CC). C++ builds H_site, which
    commutes with G by construction, and forms h_Γ = V_Γ^T H_site V_Γ per irrep. It diagonalises
    each one and stores energies plus eigenorbitals for *every* irrep, so m = 1 levels get energies
    too. For NV this reproduces exactly
    `H_A1 = [[ε_N, √3 β_NC], [√3 β_NC, ε_C + 2 β_CC]]`.
  - Parameters default to 0 and are entered by the user. A future NV preset would be labelled
    "schematic model", with its source, never as a physical default.
  - Diagonalisation: a small real-symmetric Jacobi (m ≤ ~10) in `Domain/Math`. First check whether
    `ScenePlaneGeometry`'s 3×3 eigen code generalises.
  - Complex pairs: a real symmetric h is applied identically to the u_k and v_k copies.
  - Output type `MixedOrbital {irrepLabel, level, irrepRow, energy (optional), coefficients}`. It is
    drawable through `BuildSalcSceneOrbital` and named "<Γ> poziom k (model)", never a1/a1'.
- **Tests.** Pair orbits for NV. h_Γ for NV equals the closed form above. U applied to all rows
  keeps the partner property. θ = 0 is the identity. Eigenvalues of H_site equal the union of the
  irrep eigenvalues with multiplicity d.
- **Acceptance.** NV A1 copies mix into two model levels whose order follows the entered
  parameters. E is unchanged (m = 1).
- **Out of scope.** Overlap S and the generalised problem Hc = ESc. Parameter fitting. Löwdin.

## 58 - Many-electron densities via the 1-RDM

- **Goal.** For a multiplet state, draw natural orbitals, total density and spin density, and later
  density differences between states, as "active-space" quantities.
- **Physics.**
  - γ_{pσ,qτ} = ⟨Ψ|a†_{pσ} a_{qτ}|Ψ⟩ = Σ_IJ C_I* C_J ⟨D_I|a†a|D_J⟩. The spin-summed γ gives
    natural orbitals with 0 ≤ n_k ≤ 2.
  - ρ_s comes from γ^α − γ^β, using M_S = S by default.
  - Degenerate rows are averaged incoherently: γ̄ = (1/d) Σ_r γ^(r).
  - For real basis functions only Re γ contributes to ρ, so complex-irrep active spaces still give
    real densities.
- **Architecture.**
  - Python, at analysis time, beside `wavefunction_payload`, where the determinant phase
    convention lives. Occupations are bitsets. For each ket and each occupied q, apply a_q with
    its fermionic sign, then a†_p with its sign, and look the result up in a determinant -> index
    map. Returns γ^α and γ^β per state over the active spatial orbitals.
  - Binding `ActiveOrbitalRef {irrepLabel, occurrenceIndex, irrepRow, source (Salc|Mixed)}` maps
    each active spatial orbital to a drawable orbital. It is derived from the active-space
    selection, which already knows (irrep, occurrence) per shell. It needs no scene key: the
    density object builds its own components.
  - Rendering: a density is not ψ. Diagonalise γ (or γ^α − γ^β) to get ρ = Σ_k w_k |φ̃_k|², with
    signed w_k for spin. The sampler gets a "weighted sum of squares" mode over LCAO orbitals. Its
    ± isosurfaces reuse the existing two-colour phase display. A new `SceneOrbital` kind, or a
    sibling scene object, is decided in the task file after reading the sampler.
  - Natural orbitals are LCAOs and render with no renderer change at all.
  - UI label: "Gęstość w przestrzeni aktywnej" (active-space density) /
    "Różnica gęstości w przestrzeni aktywnej" (active-space density difference). Never presented
    as DFT.
- **Tests.**
  - Tr γ = N and Tr γ^α = N_α.
  - A single determinant gives an idempotent γ.
  - The fermionic sign agrees with a brute-force determinant expansion on 3 to 4 orbitals.
  - NV ³A₂ (M_S = 1) gives a spin density of |e_x|² + |e_y|² and a total density of
    2|a1|² + 2|a1'|² + |e_x|² + |e_y|² (in the SALC basis).
  - Natural occupations lie in [0, 2].
- **Acceptance.** NV ³A₂ spin density shows the e-orbital pattern on the three carbons. The
  ³A₂ -> ³E density difference is drawn and labelled as active-space.
- **Out of scope.** Ψ(r₁..r_N) as a field. Overlap and normalisation (below). Spin-orbit.
  Densities from DFT grids, which belong to the separate CHGCAR/cube workstream.

## Deliberately deferred

- **Overlap S, normalisation, Löwdin.**
  - The renderer draws the isosurface at f·max|ψ|, so ψ -> Aψ leaves the surface unchanged at a
    fixed `isoFraction`.
  - S_ij, ‖ψ‖² = c†Sc, φS^{-1/2} and Hc = ESc only matter once the model Hamiltonian of task 57 is
    meant quantitatively.
- **Radial model.** Hydrogenic + Slater Z_eff + real Y_lm is enough to show SALCs. STO-3G buys
  analytic Gaussian integrals, which a grid-sampled picture does not use. The real quality jump is
  imported grids: KS orbitals, Wannier functions, CHGCAR/cube, WAVECAR. If the radial part is ever
  generalised, use `RadialModel {Hydrogenic, Slater, ImportedGrid}`.

## Risk

- **Highest mathematical risk:** the 58 fermionic signs and the 1-RDM phase convention. A sign
  error gives plausible-looking but wrong densities. Mitigate with a brute-force determinant
  cross-check in the tests. Second: 54 complex pairing, mitigated by exact character comparison
  and the partner test.
- **Highest architectural-debt risk:** 58's density rendering, which bends the "one ψ per
  SceneOrbital" model, and 55's vacancy visibility, where domain-owned objects need per-object
  view flags without stable ids. Both are to be decided in their task files after reading the
  code, not improvised during implementation.
