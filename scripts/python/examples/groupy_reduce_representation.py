from __future__ import annotations

import json
import sys

try:
    import numpy as np
    import sympy as sp
    from sympy import GramSchmidt
    from groupy import PointGroup
    from groupy.repr.repr import RepresentationSymb
except ImportError as exc:
    print(json.dumps({"error": "groupy_not_installed", "detail": str(exc)}), file=sys.stderr)
    raise SystemExit(1)


def fail(message: str, error: str = "groupy_analysis_failed") -> None:
    print(json.dumps({"error": error, "detail": message}), file=sys.stderr)
    raise RuntimeError(message)


def build_representation(payload: dict, point_group: PointGroup) -> RepresentationSymb:
    sites = payload["sites"]
    tolerance = float(payload["matchTolerance"])
    if not sites:
        fail("The basis site list is empty.", "empty_basis")
    if tolerance < 0:
        fail("matchTolerance must be non-negative.", "invalid_match_tolerance")

    positions = np.array([site["position"] for site in sites], dtype=float)
    matrices = []
    for element in point_group.elements:
        rotated = (np.asarray(element.matrix_num, dtype=float) @ positions.T).T
        permutation = []
        for source_index, rotated_position in enumerate(rotated):
            distances = np.linalg.norm(positions - rotated_position, axis=1)
            matches = np.flatnonzero(distances <= tolerance)
            if len(matches) != 1:
                fail(
                    f"Group element {element} rotates site {source_index} to no unique site "
                    f"within matchTolerance={tolerance}.",
                    "basis_not_closed",
                )
            permutation.append(int(matches[0]))

        matrix = sp.zeros(len(sites))
        for source_index, target_index in enumerate(permutation):
            matrix[source_index, target_index] = 1
        matrices.append(matrix)
    return RepresentationSymb(matrices, point_group=point_group)


def coefficient_payload(coefficient: sp.Expr) -> dict:
    exact = str(sp.simplify(coefficient))
    return {"exact": exact, "numeric": float(sp.sympify(exact))}


def reduce_representation(payload: dict) -> dict:
    label = str(payload["pointGroupLabel"])
    point_group = PointGroup(label)
    representation = build_representation(payload, point_group)
    reduction = point_group.ireps.reduce_repr(representation)

    decomposition = []
    projected_vectors = []
    for irrep_label, multiplicity in reduction.items():
        multiplicity = int(multiplicity)
        if multiplicity <= 0:
            continue
        irrep = point_group.ireps[irrep_label].get_repr()
        dimension = int(irrep.shape[-1])
        decomposition.append(
            {"irrepLabel": irrep_label, "multiplicity": multiplicity, "dimension": dimension}
        )
        for row in range(dimension):
            occurrence_index = 0
            projector = irrep.generalized_projection_operator(representation, row, row)
            _, pivots = projector.rref()
            orthonormal = GramSchmidt(
                [projector.col(column) for column in pivots], orthonormal=True
            )
            for vector in orthonormal:
                projected_vectors.append(
                    {
                        "irrepLabel": irrep_label,
                        "occurrenceIndex": occurrence_index,
                        "irrepRow": row,
                        "coefficients": [coefficient_payload(vector[index]) for index in range(len(payload["sites"]))],
                    }
                )
                occurrence_index += 1

    return {
        "pointGroupLabel": label,
        "siteLabels": [str(site["label"]) for site in payload["sites"]],
        "groupOrder": len(point_group.elements),
        "decomposition": decomposition,
        "projectedVectors": projected_vectors,
    }


def main() -> int:
    if len(sys.argv) < 2:
        print(json.dumps({"error": "missing_payload", "detail": "Expected a payload JSON path."}), file=sys.stderr)
        return 1
    try:
        with open(sys.argv[1], "r", encoding="utf-8") as handle:
            payload = json.load(handle)
        print(json.dumps(reduce_representation(payload), separators=(",", ":")))
        return 0
    except Exception as exc:
        if not isinstance(exc, RuntimeError):
            print(json.dumps({"error": "groupy_analysis_failed", "detail": str(exc)}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
