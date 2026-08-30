from __future__ import annotations

import json
import sys

try:
    import numpy as np
    from scipy.optimize import linear_sum_assignment
except ImportError as exc:
    print(json.dumps({"error": "scipy_not_installed", "detail": str(exc)}), file=sys.stderr)
    raise SystemExit(1)


def solve_one(matrix: dict) -> list[int]:
    comparison_count = int(matrix["comparison_count"])
    reference_count = int(matrix["reference_count"])
    comparison_to_reference = [-1] * comparison_count

    if comparison_count == 0 or reference_count == 0:
        return comparison_to_reference

    costs = np.array(matrix["costs"], dtype=float).reshape(comparison_count, reference_count)
    # linear_sum_assignment on a rectangular matrix returns exactly min(rows, cols) pairs, NOT one
    # per row - any comparison row with no returned pair (only possible when reference_count <
    # comparison_count) stays -1 above. This is scipy's own documented rectangular-assignment
    # behavior, not something this script has to special-case.
    row_indices, col_indices = linear_sum_assignment(costs)
    for row, col in zip(row_indices.tolist(), col_indices.tolist()):
        comparison_to_reference[row] = col
    return comparison_to_reference


def solve_batch(payload_path: str) -> dict:
    """Batch entry point: solves every local component's matrix in one process, since each of the
    (potentially many) local matching components from Domain/Crystal/StructureComparison.cpp is far
    too small to be worth its own subprocess round-trip (cold `import scipy` alone dominates)."""
    with open(payload_path, "r", encoding="utf-8") as handle:
        payload = json.load(handle)

    matrices = payload["matrices"]
    return {"assignments": [solve_one(matrix) for matrix in matrices]}


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit("usage: scipy_hungarian_assignment.py <batch_payload_json_path>")

    print(json.dumps(solve_batch(sys.argv[1])))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
