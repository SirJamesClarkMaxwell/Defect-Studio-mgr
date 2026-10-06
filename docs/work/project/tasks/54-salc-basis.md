# Task 54: correct SALC basis + real basis of complex irreps

First task of `docs/work/project/plans/defect-electronic-structure.md`. Read its section
"Verification of the task-53 SALC code" first: it states the three bugs this task fixes and how
they were confirmed.

Branch: `task/54-salc-basis` (from `task/53-salc-orbitals`).

## Goal

The Group Theory panel's projected vectors become true symmetry-adapted copies:

- every row of copy k of Γ is generated from the same seed;
- the copies transform as partners, `D(g) v_r = Σ_t Γ_tr(g) v_t`;
- complex irreps carry their correct labels.

For every pair of complex-conjugate irreps (E+/E- of C3, C4, S4, C6, C3h, C4h, C6h, S6, T, Th) the
analysis also returns a real basis, u = √2 Re z and v = √2 Im z, and the panel can draw it with
the task-53 LCAO path.

## One task or 54a + 54b?

**One task, one branch, two commits.**

- **Phase A, Python only:** consistency, transpose and irrep-matrix fixes.
- **Phase B:** realification, the C++ contract, validation and UI.

Phase A alone is about 60 lines of Python, and Phase B cannot be tested without it: realification
needs correctly labelled complex vectors. Both phases share the same new module and the same
partner-property test harness. Splitting into two task files would duplicate that harness and the
review. The commit boundary gives the same review checkpoint: review the Phase A commit before
Phase B starts.

## Background: what is wrong now (verified)

1. **Inconsistent copies for m_Γ > 1.**
   - `reduce_representation` runs `P_rr` per row, then `rref`, then `GramSchmidt`, and numbers the
     results per row.
   - The image of P_rr is the m-dimensional row-r subspace, and Gram-Schmidt picks an arbitrary
     basis of it.
   - Result: for C3v with a 6-site orbit (E, m=2) or Td with 24 sites, "copy k" rows fail the
     partner test and do not even span an invariant subspace.
2. **Transposed permutation matrices.**
   - `matrix[source, target] = 1` is the matrix of g⁻¹, an anti-homomorphism.
   - Characters and real irreps are unaffected.
   - Complex irreps get E+ ↔ E- swapped. Transfer operators would be wrong for non-abelian groups.
3. **Complex default matrices for real-type irreps.**
   - Some cubic irreps (T2 of O, T2g/T2u of Oh) have complex default matrices from `get_repr()`.
   - groupy's `ActiveSpace` uses `get_repr(real=True)` for them. The SALC code must use the same
     matrices, so that SALC row r is the same partner function as active-space orbital row r.

## The algorithm (Python, validated on the dev machine)

New module `scripts/python/examples/salc_basis.py`. It is our own code and calls only the groupy
API the scripts already use: `PointGroup`, `ireps[...]`, `.get_repr(real=...)`,
`.frobenius_schur`, `.values`, `.generalized_projection_operator`, `RepresentationSymb`,
`ireps.reduce_repr`. **Do not copy anything from `Vendor/groupy` into the repo.**

```python
class SalcError(RuntimeError):            # .code, .detail - like AnalysisError
def permutation_matrices(permutations: list[list[int]]) -> list[sp.Matrix]
def irrep_matrices(irrep) -> RepresentationSymb
def conjugate_partner(point_group, label: str) -> str | None
def symmetry_adapted_basis(point_group, permutations: list[list[int]]) -> dict
def coefficient_payload(value: sp.Expr) -> dict   # moved here from groupy_point_group_analysis.py, unchanged
```

1. **`permutation_matrices`.** `permutations[g][s]` is the site that element g sends site s to (what
   `build_representation` already computes). The matrix is `M[permutations[g][s], s] = 1`, so
   T(g) e_s = e_{g·s}. It must be a homomorphism under groupy's element product: the test checks
   `M(g) M(h) == M(g @ h)`.
2. **`irrep_matrices(irrep)`.** Return `r = irrep.get_repr()`. If `irrep.frobenius_schur == 1` and any
   entry of `r` has a non-zero imaginary part, return `irrep.get_repr(real=True)` instead. This is
   the same rule groupy's ActiveSpace applies, restated, not copied.
3. **`conjugate_partner(pg, a)`.** Defined only when `pg.ireps[a].frobenius_schur == 0`. It returns the
   unique other label b with `sp.simplify(sp.sympify(χ_b[c]) - sp.conjugate(sp.sympify(χ_a[c]))) == 0`
   for every class c, using **exact** characters (`.values`). It does **not** use label names. It
   raises `SalcError("conjugate_partner_missing")` if there is none or more than one.
4. **`symmetry_adapted_basis(pg, permutations)`.**
   - `rep = RepresentationSymb(permutation_matrices(permutations), point_group=pg)`.
   - `reduction = pg.ireps.reduce_repr(rep)`.
   - Process irreps with multiplicity m > 0 in `pg.ireps.keys()` order. This is also the
     `decomposition` order.
   - Let `ν = frobenius_schur`:
     - ν == -1: raise `SalcError("quaternionic_irrep")`. This does not occur for single
       crystallographic groups; it is refused explicitly.
     - ν == 0 and Γ is the *secondary* member of its pair, meaning its partner comes earlier in
       table order: its vectors are the **exact complex conjugates** of the primary's, with the
       same occurrence and row 0. Do not project Γ* independently. Independent Gram-Schmidt would
       give w_k = Σ_l U_kl z_l*, and the pairing of copy k with copy k would break for m > 1.
     - Otherwise (real-type, or the primary of a pair):
       - `G = irrep_matrices(irrep)` and `d = G.shape[-1]`. For ν == 0, d must be 1, otherwise
         raise `SalcError("complex_irrep_dimension")`.
       - Seeds:
         - `P00 = G.generalized_projection_operator(rep, 0, 0)`;
         - `_, pivots = P00.rref()`;
         - `seeds = GramSchmidt([P00.col(c) for c in pivots], orthonormal=True)`.
         - If `len(seeds) != m`, raise `SalcError("projection_rank_mismatch")`.
       - **Phase rule** for each seed:
         - find the first coefficient c0 with `sp.simplify(c0) != 0`;
         - multiply the seed by `sp.conjugate(c0) / sp.Abs(c0)`;
         - simplify each entry with `sp.simplify(sp.expand_complex(.))`.
         - The first non-zero coefficient of every seed is then real and positive. For real
           irreps this is the sign rule.
       - Partners: for each seed k and row r = 0..d-1,
         `v_r = (G.generalized_projection_operator(rep, r, 0) * v_0).applyfunc(simplify∘expand_complex)`.
         Row 0 reproduces the seed.
   - **projectedVectors** order: irrep (table order), then occurrence k, then row r.
   - **realPairVectors**: for each *primary* complex irrep Γ (table order) with partner Γ*, for each
     occurrence k with z = its row-0 vector, emit two entries:

     ```
     {"irrepLabel": Γ, "conjugateIrrepLabel": Γ*, "occurrenceIndex": k, "irrepRow": 0,
      "coefficients": [coefficient_payload(simplify(sqrt(2) * re(z_i)))]}   # u
     {..., "irrepRow": 1, "coefficients": [coefficient_payload(simplify(sqrt(2) * im(z_i)))]}  # v
     ```

     z ⟂ z* (they belong to different irreps), so u and v are orthonormal and real, and they span
     {z, z*}.
   - Returns `{"decomposition": [...], "projectedVectors": [...], "realPairVectors": [...]}`. The
     decomposition entries are unchanged in shape.

`groupy_point_group_analysis.py`:

- `build_representation` keeps its closure check and still returns the permutations; its sympy
  matrix building goes away.
- `reduce_representation` calls `symmetry_adapted_basis`. The `reduction` JSON object gains
  `realPairVectors`.
- `main` also catches `SalcError` and prints its `code`/`detail` the way `AnalysisError` is printed.
- Nothing in the multiplet path (`active_shells`, `wavefunction_payload`, `ActiveSpace`, term table,
  tensor power) changes.

`groupy_reduce_representation.py` (the spike): build `permutations` exactly as now, then use
`symmetry_adapted_basis(...)["projectedVectors"]` and `["decomposition"]`. The script is run from
its own directory, so `import salc_basis` resolves, and `prepare_app_python_runtime.py` copies the
whole `examples/` folder.

## The C++ contract (written before dispatch - already in the tree)

`src/Domain/Symmetry/PointGroupAnalysis.hpp`:

```cpp
	// One symmetry-adapted basis vector projected out of the reducible representation.
	// (keep the existing labelling paragraph)
	//
	// task/54 invariant, checked by ValidateSymmetryAdaptedBasis at the bridge: for every
	// decomposition entry (Γ, m, d) there are exactly m*d vectors, occurrences 0..m-1 x rows
	// 0..d-1, listed irrep (decomposition order) -> occurrence -> row. All rows of one
	// (irrepLabel, occurrenceIndex) come from the same seed through the transfer operators
	// P_r0 = (d/|G|) Σ_g Γ_r0(g)* D(g), so they transform as partners:
	// D(g) v_r = Σ_t Γ_tr(g) v_t. That is what makes "copy k" a meaningful unit for mixing (task 57)
	// and for active-space orbitals. Which orthonormal basis of the m copies is returned is still
	// arbitrary (projection order); physical copies need a Hamiltonian.
	// Phase: the first non-zero coefficient of each copy's row 0 is real and positive.
	struct SymmetryAdaptedVector
	{
		std::string irrepLabel;
		int occurrenceIndex = 0;
		int irrepRow = 0;
		std::vector<ExactCoefficient> coefficients;
		// task/54: empty in projectedVectors. In realPairVectors: Γ*, the complex-conjugate partner of
		// irrepLabel (Γ); irrepRow is then the component - 0 = u = √2 Re z, 1 = v = √2 Im z, with z
		// copy occurrenceIndex of Γ - and every coefficient is real (numericImaginary == 0).
		std::string conjugateIrrepLabel;
	};

	struct PointGroupReduction
	{
		// ... existing fields ...
		// task/54: the real basis of each pair of complex-conjugate irreps (Frobenius-Schur
		// indicator 0, e.g. E+/E- of C3), two vectors per copy of the pair, ordered pair (table
		// order of Γ) -> occurrence -> component. Pairs are found by exact character comparison
		// (χ_Γ* = conj χ_Γ on every class), never by name. projectedVectors of Γ* are the exact
		// conjugates of those of Γ, copy for copy, so copy k of Γ and of Γ* belong together.
		// u, v is one choice of real basis among all rotations in the (u, v) plane, not a group-theory
		// result (e_x/e_y names are a choice too). Empty when the group has no complex irreps.
		std::vector<SymmetryAdaptedVector> realPairVectors;
	};
```

New `src/Domain/Symmetry/SymmetryAdaptedBasis.hpp` (Domain, no other includes than
PointGroupAnalysis.hpp and StructuredError.hpp):

```cpp
	// task/54: the bridge's trust check on a reduction coming back from Python. Errors
	// (StructuredError, category Validation, source "Domain/Symmetry/SymmetryAdaptedBasis"):
	//   "symmetry.salc.unknown_irrep"      - a vector's irrepLabel is not in decomposition
	//   "symmetry.salc.incomplete_copy"    - an irrep's vectors are not exactly occurrences
	//                                         0..m-1 x rows 0..d-1 (missing or duplicate)
	//   "symmetry.salc.order"              - projectedVectors not irrep -> occurrence -> row
	//   "symmetry.salc.coefficient_count"  - any vector (either list) whose coefficients.size()
	//                                         != siteLabels.size()
	//   "symmetry.salc.real_pair"          - a realPairVectors entry with an empty
	//                                         conjugateIrrepLabel, Γ or Γ* missing from decomposition,
	//                                         unequal multiplicities of Γ and Γ*, not exactly
	//                                         components {0, 1} per occurrence 0..m-1, or any
	//                                         |numericImaginary| >= 1e-12
	// A reduction with empty projectedVectors and realPairVectors is valid (nothing projected).
	[[nodiscard]] Result<void> ValidateSymmetryAdaptedBasis(const PointGroupReduction &reduction);
```

`src/Renderer/Scene/SceneOrbitalLcao.hpp`: only the `complex_coefficient` comment changes. It now
says that the drawable real basis of a complex pair is `PointGroupReduction::realPairVectors`
(task/54). The check itself stays as the safety net.

## Files to create or change

| File | Change |
|---|---|
| `scripts/python/examples/salc_basis.py` | **new**, the algorithm above |
| `scripts/python/examples/groupy_point_group_analysis.py` | delegate to `salc_basis`; emit `realPairVectors`; catch `SalcError` |
| `scripts/python/examples/groupy_reduce_representation.py` | delegate to `salc_basis` (projectedVectors + decomposition only) |
| `scripts/python/tests/test_salc_basis.py` | **new**, tests below |
| `src/Domain/Symmetry/PointGroupAnalysis.hpp` | contract above (fields + comments) |
| `src/Domain/Symmetry/SymmetryAdaptedBasis.{hpp,cpp}` | **new**, validator |
| `src/ScientificRuntime/Python/GroupTheoryBridge.cpp` | parse `realPairVectors` (`json.value(..., array())`, `conjugateIrrepLabel` via `.at`) in `Analyze`; run `ValidateSymmetryAdaptedBasis` on the reduction in both `Analyze` and `ReduceRepresentation` and return its error; add `"quaternionic_irrep"`, `"projection_rank_mismatch"`, `"conjugate_partner_missing"`, `"complex_irrep_dimension"` to `knownCodes` |
| `src/Presentation/Panels/GroupTheoryPanel.hpp` | `drawSalcButton(const SymmetryAdaptedVector &, const std::string &name)`; new `drawRealPairVectors()` |
| `src/Presentation/Panels/GroupTheoryPanelOrbitals.cpp` | button refactor, complex-row disabling, real-pair table (below) |
| `src/Presentation/Panels/GroupTheoryPanelTables.cpp` | only: compute the projected row's name (existing rule, moved from `drawSalcButton`) and call the new signature; call `drawRealPairVectors()` right after `drawProjectedVectors()` (line ~215). It is at 499 lines: move the name rule into `GroupTheoryPanelOrbitals.cpp` as a private helper rather than growing this file |
| `src/Renderer/Scene/SceneOrbitalLcao.hpp` | comment only |
| `tests/Domain/Symmetry/SymmetryAdaptedBasisTests.cpp` | **new** |
| `tests/ScientificRuntime/PointGroupAnalysisBridgeTests.cpp` | new cases below |
| `docs/work/project/plans/defect-electronic-structure.md` | status line |

Regenerate projects after adding the `.hpp`/`.cpp`/test files: `DS_TOOLSET=msc-v143` and
`scripts\Windows\GenerateProjects.bat`.

### UI details

- **Projected-vectors table.** A row whose vector has any `|numericImaginary| >= 1e-9` gets its
  "Rysuj" disabled, with the tooltip "Wektor zespolony - narysuj Re/Im z tabeli „Baza rzeczywista”
  poniżej."
- **`drawRealPairVectors()`.** Only when `realPairVectors` is non-empty.
  - Heading: "Baza rzeczywista par zespolonych". Below it, `TextDisabled`: "u = √2 Re z,
    v = √2 Im z - jeden z równoważnych wyborów bazy (obrót w płaszczyźnie u, v), nie wynik teorii
    grup."
  - Table columns:
    - Para: `MathLabel(FormatIrrepLabel(Γ) + " \\oplus " + FormatIrrepLabel(Γ*))`.
    - Kopia: occurrence + 1.
    - Składowa: "u (Re)" or "v (Im)".
    - One column per site: the existing `Cell` helper.
    - "Rysuj": `drawSalcButton(vector, name)` with name `"<Γ>⊕<Γ*> #<k+1> Re"` / `"... Im"`,
      using raw labels.
  - Same staleness and window rules as the existing button. No active, order or physical-label
    columns.
- `drawSalcButton` keeps everything else: staleness, missing window, error reporting, undo snapshot
  and `AppendSceneOrbital`.

## Files that must NOT be touched

- `Vendor/**`. Nothing from groupy, puntukas_tools2 or dephonopy is copied into the repo, not even
  helper snippets.
- The multiplet path of `groupy_point_group_analysis.py`: `active_shells`, `wavefunction_payload`,
  ActiveSpace, term table, tensor power, detection and frame alignment, `coefficient_payload`
  behaviour.
- `SceneOrbitalLcao.cpp`, `SceneOrbitalGeometry.*`, the renderer, IO/persistence and
  `GroupTheoryFormatting*`. Markdown/LaTeX export of the real basis is a follow-up, not this task.
- `BuildActiveSpaceSelection` semantics (it keys copies by irrep + occurrence, which is now
  correct, not changed).
- The contract declarations and comments above, and the test expectations, once written.

## Tests

### Python - `scripts/python/tests/test_salc_basis.py` (the mathematical acceptance)

Write it with `unittest`, like the existing tests there. It runs under pytest:
`.venv/Scripts/python.exe -m pytest scripts/python/tests/test_salc_basis.py -q`.

- `setUpModule` calls `skipTest` when `groupy` cannot be imported. groupy is private and missing on
  CI; on the dev machine the test must run.
- Fixtures build site sets as orbits of a seed point under the group:
  `{g.matrix_num @ p}` deduplicated at 1e-6. Permutations are built as the script builds them.
- Partner check (own code, a few lines):

  ```python
  D = [np.array(M.tolist(), dtype=complex) for M in permutation_matrices(perms)]
  G = [np.array(X.tolist(), dtype=complex) for X in irrep_matrices(pg.ireps[label])]
  V = np.column_stack([vec(label, k, r) for r in range(d)])        # copy k
  assert all(np.allclose(D[i] @ V, V @ G[i], atol=1e-9) for i in range(len(pg.elements)))
  ```

| Test | Case | Asserts |
|---|---|---|
| `test_permutation_matrices_are_a_homomorphism` | C3v NV cluster, Td 4-site | `D(g)D(h) == D(g @ h)` for all g, h |
| `test_every_copy_transforms_as_partners` | C3v 4 sites (NV: 2A1+E), C3v 6-site general orbit (E m=2), C3v 9 sites (E m=3), Td 24 sites (E m=2, T1 m=3, T2 m=3), D3d 12, C3 6 sites, C4h 8, S4 4, C6 6, C3h 6, S6 6, T 12, Th 12 | partner check for every (Γ, k); also fails on the pre-fix script (keep a comment saying so) |
| `test_basis_is_orthonormal_and_complete` | same cases | all projectedVectors together: V^H V = I (Hermitian) and count == site count |
| `test_order_is_irrep_occurrence_row` | C3v 6-site, Td 24 | sequence of (irrep, k, r) equals the sorted expectation, irreps in `pg.ireps.keys()` order |
| `test_phase_rule` | same cases | first non-zero coefficient of each row-0 vector: exact `sp.im(c) == 0` and `c > 0` |
| `test_conjugate_partners_by_exact_characters` | C3, C3h, C6, T, Th, C3v | C3 {E+: E-}; C3h {E'+: E'-, E"+: E"-}; C6 {E1+: E1-, E2+: E2-}; T {E+: E-}; Th {Eg+: Eg-, Eu+: Eu-}; every ν = +1 irrep has no partner; C3v has no pairs |
| `test_conjugate_irrep_vectors_are_exact_conjugates` | C3 6 sites (m=2), C6 6 | vectors of Γ* copy k == conj(vectors of Γ copy k), exact `sp.simplify(a - conj(b)) == 0` |
| `test_real_pair_vectors` | C3 3-site triangle, C3 6 sites, C4h 8, T 12, C6h 12 | per (pair, k) exactly components 0, 1; every coefficient exactly real (`sp.im == 0`) and its `exact` string contains no `re(`, `im(`, `I`; together with all real-type projectedVectors they form a real orthonormal basis of size n; each (u_k, v_k) span is invariant under every D(g): `P D(g) W == D(g) W` with W = [u v], P = W W^T |
| `test_c3_triangle_real_pair_is_textbook` | C3 triangle (1,0,0.3), (-1/2, ±√3/2, 0.3) | u exact == [sqrt(6)/3, -sqrt(6)/6, -sqrt(6)/6]; v[0] == 0; v[1] == -v[2]; \|v[1]\| == sqrt(2)/2. Do not assert the sign of v: E+ vs E- depends on the frame orientation |
| `test_real_type_irrep_with_complex_default_matrices_stays_real` | Oh, 12-site cuboctahedron orbit of (1,1,0) (A1g+Eg+T2g+T1u+T2u) | all T2g/T2u coefficients real; partner check with `irrep_matrices` |
| `test_quaternionic_irreps_are_refused` | monkeypatch an irrep's `frobenius_schur` to -1 on a C3v instance (or call the branch with a stub) | `SalcError` with code `quaternionic_irrep` |

### C++ - `tests/Domain/Symmetry/SymmetryAdaptedBasisTests.cpp` (no Python)

Build `PointGroupReduction` literals: NV-like (A1 m=2 d=1, E m=1 d=2, 4 sites) and C3-like (A, E+,
E-, 3 sites, with a real pair).

| Test | Asserts |
|---|---|
| AcceptsWellFormedRealReduction | ok |
| AcceptsWellFormedReductionWithRealPairs | ok |
| AcceptsEmptyReduction | ok |
| RejectsUnknownIrrep | `symmetry.salc.unknown_irrep` |
| RejectsMissingRow / RejectsDuplicateRow / RejectsOccurrenceGap | `symmetry.salc.incomplete_copy` |
| RejectsRowMajorOrder | the old (row -> occurrence) order for an m=2, d=2 irrep -> `symmetry.salc.order` |
| RejectsIrrepsOutOfDecompositionOrder | E listed before A1 -> `symmetry.salc.order` |
| RejectsWrongCoefficientCount | `symmetry.salc.coefficient_count` (projected and real-pair lists each) |
| RejectsRealPairWithImaginaryPart / WithoutConjugateLabel / WithUnknownConjugate / MissingComponent / WithUnequalMultiplicity | `symmetry.salc.real_pair` |

### C++ - `tests/ScientificRuntime/PointGroupAnalysisBridgeTests.cpp` (real Python, no `GTEST_SKIP`, per the file's rule)

| Test | Request | Asserts |
|---|---|---|
| ProjectedVectorsAreOrderedIrrepOccurrenceRow | NV cluster, "C3v" | (A1,0,0), (A1,1,0), (E,0,0), (E,0,1); `realPairVectors` empty |
| SixSiteOrbitHasTwoECopiesInOrder | C3v general orbit of (0.9, 0.4, 0.3) in groupy's frame, 6 C sites, "C3v" | decomposition A1 1, A2 1, E 2; order A1, A2, E(0,0), E(0,1), E(1,0), E(1,1); orthonormal (complex dot) |
| C3TriangleReturnsRealBasisOfComplexPair | triangle as in the Python test, sites "C", "C3" | decomposition A, E+, E- (m=1 each); E± vectors have non-zero imaginary parts and E- == conj(E+) numerically; `realPairVectors.size() == 2`: irrep "E+", conjugate "E-", rows 0 and 1, all `numericImaginary == 0`, u ≈ (2, -1, -1)/√6 (1e-9), u[0].exact == "sqrt(6)/3", v[0] == 0, v[1] == -v[2], u·v = u·a = v·a = 0 |
| NvClusterAnalysedAsC3HasRealPair | NV cluster, "C3" (subgroup) | `realPairVectors` non-empty, all real; validates |

The existing `GroupTheoryBridgeTests` (spike) and the other `PointGroupAnalysisBridgeTests` must
stay green unchanged.

## Acceptance criteria

1. `.venv/Scripts/python.exe -m pytest scripts/python/tests/test_salc_basis.py -q`: all pass on
   the dev machine with groupy installed.
2. Release build of `DefectStudio` and `DefectStudioTests` succeeds.
3. `DefectStudioTests --gtest_filter=SymmetryAdaptedBasisTests.*:PointGroupAnalysisBridgeTests.*:GroupTheoryBridgeTests.*:GroupTheoryFormattingTests.*:SceneOrbitalLcao*`
   passes.
4. Full suite: no new failures (known: 5 `PathStrokeMesherTests` bevel failures, 1 skip).
5. Manual check in the app:
   1. NV- supercell: select N + 3 C, centre on the vacancy, group "Detect". The projected table
      shows A1#1, A1#2, E#1 rows 1-2 (adjacent), as before, and "Rysuj" works.
   2. Same selection, group forced to "C3": E+ and E- rows show complex coefficients, their "Rysuj"
      is disabled with the tooltip, the "Baza rzeczywista" table shows u/v, and drawing u and v
      gives two real lobed orbitals that look like e_x/e_y of NV, rotated within the plane.

## Constraints

- **Layering.**
  - Python does all exact algebra.
  - C++ stores the result, validates the invariant, shows it and draws it. No symbolic algebra in
    C++.
  - Domain (`SymmetryAdaptedBasis`) has no UI, renderer or bridge includes.
  - Presentation calls `BuildSalcSceneOrbital`.
  - Renderer does not know a vector was realified.
- **Naming.**
  - No physical names (a1', e_x, "lower"/"upper") are generated anywhere.
  - Real pairs are named by their two irrep labels plus Re/Im.
- **Code rules.** No exceptions in render paths; the bridge keeps its try/catch at the subprocess
  boundary. `.cpp` files stay under ~500 lines.
- **Privacy.** Only the groupy API the scripts already import is called. groupy, puntukas_tools2 and
  dephonopy code must not appear in the diff, and they are never committed.
