from __future__ import annotations

import json
import sys

try:
    import numpy as np
    from scipy.optimize import linear_sum_assignment
except ImportError as exc:
    print(json.dumps({"error": "scipy_not_installed", "detail": str(exc)}), file=sys.stderr)
    raise SystemExit(1)


def solve_assignment(matrix_path: str) -> dict:
    with open(matrix_path, "r", encoding="utf-8") as handle:
        payload = json.load(handle)

    comparison_count = int(payload["comparison_count"])
    reference_count = int(payload["reference_count"])
    comparison_to_reference = [-1] * comparison_count

    if comparison_count == 0 or reference_count == 0:
        return {"comparison_to_reference": comparison_to_reference}

    costs = np.array(payload["costs"], dtype=float).reshape(comparison_count, reference_count)
    row_indices, col_indices = linear_sum_assignment(costs)
    for row, col in zip(row_indices.tolist(), col_indices.tolist()):
        comparison_to_reference[row] = col

    return {"comparison_to_reference": comparison_to_reference}


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit("usage: scipy_hungarian_assignment.py <cost_matrix_json_path>")

    print(json.dumps(solve_assignment(sys.argv[1])))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
