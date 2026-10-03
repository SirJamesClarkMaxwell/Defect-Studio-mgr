from __future__ import annotations

import json
import sys

try:
    import numpy as np
    from groupy import PointGroup
    from salc_basis import SalcError, symmetry_adapted_basis
except ImportError as exc:
    print(json.dumps({"error": "groupy_not_installed", "detail": str(exc)}), file=sys.stderr)
    raise SystemExit(1)


def fail(message: str, error: str = "groupy_analysis_failed") -> None:
    print(json.dumps({"error": error, "detail": message}), file=sys.stderr)
    raise RuntimeError(message)


def build_representation(payload: dict, point_group: PointGroup) -> list[list[int]]:
    sites = payload["sites"]
    tolerance = float(payload["matchTolerance"])
    if not sites:
        fail("The basis site list is empty.", "empty_basis")
    if tolerance < 0:
        fail("matchTolerance must be non-negative.", "invalid_match_tolerance")

    positions = np.array([site["position"] for site in sites], dtype=float)
    permutations = []
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

        permutations.append(permutation)
    return permutations


def reduce_representation(payload: dict) -> dict:
    label = str(payload["pointGroupLabel"])
    point_group = PointGroup(label)
    permutations = build_representation(payload, point_group)
    basis = symmetry_adapted_basis(point_group, permutations)

    return {
        "pointGroupLabel": label,
        "siteLabels": [str(site["label"]) for site in payload["sites"]],
        "groupOrder": len(point_group.elements),
        "decomposition": basis["decomposition"],
        "projectedVectors": basis["projectedVectors"],
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
    except SalcError as exc:
        print(json.dumps({"error": exc.code, "detail": exc.detail}), file=sys.stderr)
        return 1
    except Exception as exc:
        if not isinstance(exc, RuntimeError):
            print(json.dumps({"error": "groupy_analysis_failed", "detail": str(exc)}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
