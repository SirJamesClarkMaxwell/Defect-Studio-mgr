from __future__ import annotations

import sympy as sp
from sympy import GramSchmidt
from groupy.repr.repr import RepresentationSymb


class SalcError(RuntimeError):
    def __init__(self, code: str, detail: str = ""):
        super().__init__(detail or code)
        self.code = code
        self.detail = detail or code


def coefficient_payload(value: sp.Expr) -> dict:
    value = sp.simplify(value)
    real, imaginary = sp.expand_complex(value).as_real_imag()
    return {
        "exact": str(value),
        "latex": sp.latex(value),
        "numeric": float(real),
        "numericImaginary": float(imaginary),
    }


def permutation_matrices(permutations: list[list[int]]) -> list[sp.Matrix]:
    matrices = []
    for permutation in permutations:
        matrix = sp.zeros(len(permutation))
        for source, target in enumerate(permutation):
            matrix[target, source] = 1
        matrices.append(matrix)
    return matrices


def irrep_matrices(irrep) -> RepresentationSymb:
    representation = irrep.get_repr()
    if irrep.frobenius_schur == 1 and any(
        sp.simplify(sp.im(entry)) != 0 for matrix in representation for entry in matrix
    ):
        return irrep.get_repr(real=True)
    return representation


def conjugate_partner(point_group, label: str) -> str | None:
    irrep = point_group.ireps[label]
    if irrep.frobenius_schur != 0:
        return None
    partners = [other for other in point_group.ireps.keys() if other != label and all(
        sp.simplify(sp.sympify(b) - sp.conjugate(sp.sympify(a))) == 0
        for a, b in zip(irrep.values, point_group.ireps[other].values)
    )]
    if len(partners) != 1:
        raise SalcError("conjugate_partner_missing", f"Expected one conjugate partner of {label}.")
    return partners[0]


def symmetry_adapted_basis(point_group, permutations: list[list[int]]) -> dict:
    representation = RepresentationSymb(permutation_matrices(permutations), point_group=point_group)
    reduction = point_group.ireps.reduce_repr(representation)
    decomposition, projected, real_pairs = [], [], []
    copies = {}
    labels = list(point_group.ireps.keys())

    def simplify(value):
        return sp.simplify(sp.expand_complex(value))

    def payload(label, occurrence, row, vector):
        return {"irrepLabel": label, "occurrenceIndex": occurrence, "irrepRow": row,
                "coefficients": [coefficient_payload(value) for value in vector]}

    for label in labels:
        multiplicity = int(reduction[label])
        if multiplicity <= 0:
            continue
        irrep = point_group.ireps[label]
        indicator = irrep.frobenius_schur
        if indicator == -1:
            raise SalcError("quaternionic_irrep", f"Quaternionic irrep {label} is unsupported.")
        gamma = irrep_matrices(irrep)
        dimension = int(gamma.shape[-1])
        partner = conjugate_partner(point_group, label)
        if indicator == 0 and dimension != 1:
            raise SalcError("complex_irrep_dimension", f"Complex irrep {label} must be one-dimensional.")
        decomposition.append({"irrepLabel": label, "multiplicity": multiplicity, "dimension": dimension})
        if partner is not None and labels.index(partner) < labels.index(label):
            vectors = [vector.conjugate().applyfunc(simplify) for vector in copies[partner]]
            if len(vectors) != multiplicity:
                raise SalcError("projection_rank_mismatch", f"Unequal conjugate multiplicities for {label}.")
        else:
            projector = gamma.generalized_projection_operator(representation, 0, 0)
            _, pivots = projector.rref()
            seeds = GramSchmidt([projector.col(column) for column in pivots], orthonormal=True)
            if len(seeds) != multiplicity:
                raise SalcError("projection_rank_mismatch", f"Unexpected projection rank for {label}.")
            transfers = [gamma.generalized_projection_operator(representation, row, 0)
                         for row in range(dimension)]
            vectors = []
            for seed in seeds:
                first = next(value for value in seed if sp.simplify(value) != 0)
                seed = (seed * sp.conjugate(first) / sp.Abs(first)).applyfunc(simplify)
                vectors.extend((transfer * seed).applyfunc(simplify) for transfer in transfers)
            if partner is not None:
                for occurrence, vector in enumerate(vectors):
                    for row, component in enumerate((sp.re, sp.im)):
                        entry = payload(label, occurrence, row, [
                            simplify(sp.sqrt(2) * component(value)) for value in vector])
                        entry["conjugateIrrepLabel"] = partner
                        real_pairs.append(entry)
        copies[label] = vectors
        projected.extend(payload(label, index // dimension, index % dimension, vector)
                         for index, vector in enumerate(vectors))
    return {"decomposition": decomposition, "projectedVectors": projected, "realPairVectors": real_pairs}
