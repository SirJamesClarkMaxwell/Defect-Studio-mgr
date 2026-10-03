"""Task 54: the symmetry-adapted basis in scripts/python/examples/salc_basis.py.

The defining property checked throughout is the partner property: copy k of irrep G, with its rows
as the columns of V_k, satisfies D(g) V_k = V_k G(g) for every group element g. The task-53 script
(independent Gram-Schmidt per row, transposed permutation matrices) fails it for every repeated
degenerate irrep and swaps E+/E- - see docs/work/project/plans/defect-electronic-structure.md.

groupy is private and not installed on CI; the whole module is skipped without it.
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "examples"))

try:
    import numpy as np
    import sympy as sp
    from groupy import PointGroup

    from salc_basis import (
        SalcError,
        conjugate_partner,
        irrep_matrices,
        permutation_matrices,
        symmetry_adapted_basis,
    )
except ImportError as exc:  # groupy missing, or salc_basis not written yet
    IMPORT_ERROR = exc
else:
    IMPORT_ERROR = None


def setUpModule():
    if IMPORT_ERROR is not None and "groupy" in str(IMPORT_ERROR):
        raise unittest.SkipTest(f"groupy unavailable: {IMPORT_ERROR}")
    if IMPORT_ERROR is not None:
        raise IMPORT_ERROR


def orbit(point_group, seed, tol=1e-6):
    points = []
    for element in point_group.elements:
        image = np.asarray(element.matrix_num, dtype=float) @ np.asarray(seed, dtype=float)
        if all(np.linalg.norm(image - other) > tol for other in points):
            points.append(image)
    return points


def site_permutations(point_group, positions, tol=1e-6):
    """permutations[g][s] = index of the site g sends site s to (what build_representation computes)."""
    positions = np.asarray(positions, dtype=float)
    permutations = []
    for element in point_group.elements:
        rotated = (np.asarray(element.matrix_num, dtype=float) @ positions.T).T
        permutation = []
        for image in rotated:
            matches = np.flatnonzero(np.linalg.norm(positions - image, axis=1) <= tol)
            assert len(matches) == 1
            permutation.append(int(matches[0]))
        permutations.append(permutation)
    return permutations


class Case:
    def __init__(self, group, seeds):
        self.group = group
        self.point_group = PointGroup(group)
        self.positions = [p for seed in seeds for p in orbit(self.point_group, seed)]
        self.permutations = site_permutations(self.point_group, self.positions)
        self.result = symmetry_adapted_basis(self.point_group, self.permutations)
        self.D = [np.array(M.tolist(), dtype=complex) for M in permutation_matrices(self.permutations)]

    @property
    def n(self):
        return len(self.positions)

    def vector(self, entry):
        return np.array([c["numeric"] + 1j * c["numericImaginary"] for c in entry["coefficients"]])

    def exact(self, entry):
        return [sp.sympify(c["exact"]) for c in entry["coefficients"]]

    def copies(self):
        """{(irrep, k): [row vectors in row order]} from projectedVectors."""
        copies = {}
        for entry in self.result["projectedVectors"]:
            copies.setdefault((entry["irrepLabel"], entry["occurrenceIndex"]), []).append(
                (entry["irrepRow"], self.vector(entry)))
        return {key: [v for _, v in sorted(rows, key=lambda item: item[0])] for key, rows in copies.items()}


NV = ("C3v", [[1.0, 0.0, 0.5], [0.0, 0.0, 1.6]])  # 3 C + N: 2 A1 + E
PARTNER_CASES = [
    NV,
    ("C3v", [[0.9, 0.4, 0.3]]),                      # E m=2
    ("C3v", [[0.9, 0.4, 0.3], [1.0, 0.0, 0.5]]),     # E m=3
    ("Td", [[1.0, 0.3, 0.2]]),                       # E m=2, T1 m=3, T2 m=3
    ("D3d", [[1.0, 0.3, 0.2]]),
    ("C3", [[1.0, 0.2, 0.3], [0.5, -0.7, -0.4]]),    # E+/E- m=2
    ("C4h", [[1.0, 0.2, 0.3]]),
    ("S4", [[1.0, 0.2, 0.3]]),
    ("C6", [[1.0, 0.2, 0.3]]),
    ("C3h", [[1.0, 0.2, 0.3]]),
    ("S6", [[1.0, 0.2, 0.3]]),
    ("T", [[1.0, 0.3, 0.2]]),
    ("Th", [[1.0, 0.3, 0.0]]),
]
C3_TRIANGLE = ("C3", [[1.0, 0.0, 0.3]])

_CACHE = {}


def case(spec):
    key = (spec[0], str(spec[1]))
    if key not in _CACHE:
        _CACHE[key] = Case(*spec)
    return _CACHE[key]


class PermutationMatrices(unittest.TestCase):
    def test_permutation_matrices_are_a_homomorphism(self):
        for spec in [NV, ("Td", [[1.0, 1.0, 1.0]])]:
            c = case(spec)
            elements = c.point_group.elements
            for i, g in enumerate(elements):
                for j, h in enumerate(elements):
                    product = elements.index(g @ h)
                    self.assertTrue(np.allclose(c.D[i] @ c.D[j], c.D[product]), (spec[0], i, j))


class SymmetryAdaptedBasis(unittest.TestCase):
    def test_every_copy_transforms_as_partners(self):
        # Fails on the task-53 script for C3v E m>=2, Td E/T1/T2 and the E+/E- labels of every
        # complex group.
        for spec in PARTNER_CASES:
            c = case(spec)
            for (label, k), rows in c.copies().items():
                gamma = [np.array(X.tolist(), dtype=complex) for X in irrep_matrices(c.point_group.ireps[label])]
                V = np.column_stack(rows)
                for i in range(len(c.point_group.elements)):
                    self.assertTrue(np.allclose(c.D[i] @ V, V @ gamma[i], atol=1e-9), (spec[0], label, k, i))

    def test_basis_is_orthonormal_and_complete(self):
        for spec in PARTNER_CASES:
            c = case(spec)
            W = np.column_stack([c.vector(e) for e in c.result["projectedVectors"]])
            self.assertEqual(W.shape[1], c.n, spec[0])
            self.assertTrue(np.allclose(W.conj().T @ W, np.eye(c.n), atol=1e-9), spec[0])

    def test_order_is_irrep_occurrence_row(self):
        for spec in [("C3v", [[0.9, 0.4, 0.3]]), ("Td", [[1.0, 0.3, 0.2]])]:
            c = case(spec)
            table = list(c.point_group.ireps.keys())
            keys = [(table.index(e["irrepLabel"]), e["occurrenceIndex"], e["irrepRow"])
                    for e in c.result["projectedVectors"]]
            self.assertEqual(keys, sorted(keys), spec[0])
            decomposition = [d["irrepLabel"] for d in c.result["decomposition"]]
            self.assertEqual(decomposition, sorted(decomposition, key=table.index))
            for d in c.result["decomposition"]:
                expected = {(k, r) for k in range(d["multiplicity"]) for r in range(d["dimension"])}
                got = {(e["occurrenceIndex"], e["irrepRow"]) for e in c.result["projectedVectors"]
                       if e["irrepLabel"] == d["irrepLabel"]}
                self.assertEqual(got, expected, (spec[0], d["irrepLabel"]))

    def test_phase_rule(self):
        for spec in PARTNER_CASES:
            c = case(spec)
            for entry in c.result["projectedVectors"]:
                if entry["irrepRow"] != 0:
                    continue
                first = next(x for x in c.exact(entry) if sp.simplify(x) != 0)
                self.assertEqual(sp.simplify(sp.im(first)), 0, (spec[0], entry["irrepLabel"]))
                self.assertTrue(sp.re(first) > 0, (spec[0], entry["irrepLabel"]))


class ComplexIrreps(unittest.TestCase):
    def test_conjugate_partners_by_exact_characters(self):
        expected = {
            "C3": {"E+": "E-"},
            "C3h": {"E'+": "E'-", 'E"+': 'E"-'},
            "C6": {"E1+": "E1-", "E2+": "E2-"},
            "T": {"E+": "E-"},
            "Th": {"Eg+": "Eg-", "Eu+": "Eu-"},
            "C3v": {},
        }
        for group, pairs in expected.items():
            pg = PointGroup(group)
            symmetric = dict(pairs)
            symmetric.update({b: a for a, b in pairs.items()})
            for label in pg.ireps.keys():
                if pg.ireps[label].frobenius_schur == 0:
                    self.assertEqual(conjugate_partner(pg, label), symmetric[label], (group, label))
                else:
                    self.assertNotIn(label, symmetric, (group, label))

    def test_conjugate_irrep_vectors_are_exact_conjugates(self):
        for spec in [("C3", [[1.0, 0.2, 0.3], [0.5, -0.7, -0.4]]), ("C6", [[1.0, 0.2, 0.3]])]:
            c = case(spec)
            vectors = {(e["irrepLabel"], e["occurrenceIndex"]): c.exact(e) for e in c.result["projectedVectors"]}
            for (label, k), z in vectors.items():
                if c.point_group.ireps[label].frobenius_schur != 0:
                    continue
                partner = vectors[(conjugate_partner(c.point_group, label), k)]
                for a, b in zip(partner, z):
                    self.assertEqual(sp.simplify(a - sp.conjugate(b)), 0, (spec[0], label, k))

    def test_real_pair_vectors(self):
        for spec in [C3_TRIANGLE, ("C3", [[1.0, 0.2, 0.3], [0.5, -0.7, -0.4]]), ("C4h", [[1.0, 0.2, 0.3]]),
                     ("T", [[1.0, 0.3, 0.2]]), ("C6h", [[1.0, 0.2, 0.3]])]:
            c = case(spec)
            pairs = {}
            for entry in c.result["realPairVectors"]:
                self.assertEqual(conjugate_partner(c.point_group, entry["irrepLabel"]), entry["conjugateIrrepLabel"])
                for coefficient in entry["coefficients"]:
                    self.assertEqual(coefficient["numericImaginary"], 0.0)
                    self.assertEqual(sp.im(sp.sympify(coefficient["exact"])), 0)
                    for token in ("re(", "im(", "I"):
                        self.assertNotIn(token, coefficient["exact"])
                pairs.setdefault((entry["irrepLabel"], entry["occurrenceIndex"]), {})[entry["irrepRow"]] = c.vector(entry).real
            self.assertTrue(pairs, spec[0])
            for key, components in pairs.items():
                self.assertEqual(sorted(components), [0, 1], (spec[0], key))
                W = np.column_stack([components[0], components[1]])
                P = W @ W.T
                for D in c.D:
                    self.assertTrue(np.allclose(P @ D.real @ W, D.real @ W, atol=1e-9), (spec[0], key))
            real_type = [c.vector(e).real for e in c.result["projectedVectors"]
                         if c.point_group.ireps[e["irrepLabel"]].frobenius_schur == 1]
            full = np.column_stack(real_type + [v for comps in pairs.values() for v in comps.values()])
            self.assertEqual(full.shape[1], c.n, spec[0])
            self.assertTrue(np.allclose(full.T @ full, np.eye(c.n), atol=1e-9), spec[0])

    def test_c3_triangle_real_pair_is_textbook(self):
        c = case(C3_TRIANGLE)
        pair = c.result["realPairVectors"]
        self.assertEqual([(e["irrepLabel"], e["occurrenceIndex"], e["irrepRow"]) for e in pair],
                         [("E+", 0, 0), ("E+", 0, 1)])
        u, v = (c.exact(e) for e in pair)
        self.assertEqual([sp.simplify(x - y) for x, y in zip(u, [sp.sqrt(6) / 3, -sp.sqrt(6) / 6, -sp.sqrt(6) / 6])],
                         [0, 0, 0])
        self.assertEqual(v[0], 0)
        self.assertEqual(sp.simplify(v[1] + v[2]), 0)
        self.assertEqual(sp.simplify(sp.Abs(v[1]) - sp.sqrt(2) / 2), 0)  # sign follows the frame: not asserted


class RealTypeIrreps(unittest.TestCase):
    def test_real_type_irrep_with_complex_default_matrices_stays_real(self):
        c = case(("Oh", [[1.0, 1.0, 0.0]]))  # cuboctahedron: A1g + Eg + T2g + T1u + T2u
        labels = {e["irrepLabel"] for e in c.result["projectedVectors"]}
        self.assertTrue({"T2g", "T2u"} <= labels)
        for entry in c.result["projectedVectors"]:
            for coefficient in entry["coefficients"]:
                self.assertEqual(coefficient["numericImaginary"], 0.0, entry["irrepLabel"])
        self.assertEqual(c.result["realPairVectors"], [])

    def test_quaternionic_irreps_are_refused(self):
        pg = PointGroup("C3v")
        irrep_type = type(pg.ireps["E"])
        original = irrep_type.frobenius_schur
        try:
            irrep_type.frobenius_schur = property(lambda self: -1 if self.name == "E" else 1)
            with self.assertRaises(SalcError) as raised:
                symmetry_adapted_basis(pg, site_permutations(pg, orbit(pg, [1.0, 0.0, 0.5])))
            self.assertEqual(raised.exception.code, "quaternionic_irrep")
        finally:
            irrep_type.frobenius_schur = original


if __name__ == "__main__":
    unittest.main()
