from __future__ import annotations

import json
import sys

try:
    import numpy as np
    import sympy as sp
    from pymatgen.core import Molecule
    from pymatgen.symmetry.analyzer import PointGroupAnalyzer
    from sympy import GramSchmidt
    from groupy import PointGroup
    from groupy.multiplets.terms import ActiveSpace
    from groupy.repr.repr import RepresentationSymb
except ImportError as exc:
    print(json.dumps({"error": "groupy_not_installed", "detail": str(exc)}), file=sys.stderr)
    raise SystemExit(1)


class AnalysisError(RuntimeError):
    def __init__(self, code: str, detail: str):
        super().__init__(detail)
        self.code = code
        self.detail = detail


def fail(message: str, error: str) -> None:
    raise AnalysisError(error, message)


def coefficient_payload(value: sp.Expr) -> dict:
    value = sp.simplify(value)
    real, imaginary = sp.expand_complex(value).as_real_imag()
    return {
        "exact": str(value),
        "numeric": float(real),
        "numericImaginary": float(imaginary),
    }


def normalize(vector: np.ndarray) -> np.ndarray:
    length = np.linalg.norm(vector)
    return vector / length if length > 1e-12 else vector


def frame_candidates(positions: np.ndarray, analyzer: PointGroupAnalyzer) -> list[np.ndarray]:
    axes = []
    for axis in getattr(analyzer, "rot_sym", []):
        axes.append(normalize(np.asarray(axis[0], dtype=float)))
    principal = np.asarray(getattr(analyzer, "principal_axes", np.eye(3)), dtype=float)
    axes.extend(normalize(principal[index]) for index in range(3))
    axes.extend(normalize(principal[:, index]) for index in range(3))
    axes.extend(normalize(positions[index]) for index in range(len(positions)))

    candidates = []
    for z in axes:
        if np.linalg.norm(z) < 1e-12:
            continue
        for source in list(positions) + [np.eye(3)[index] for index in range(3)]:
            x = source - np.dot(source, z) * z
            if np.linalg.norm(x) < 1e-8:
                continue
            x = normalize(x)
            y = normalize(np.cross(z, x))
            x = normalize(np.cross(y, z))
            rotation = np.vstack((x, y, z))
            if np.linalg.det(rotation) > 0:
                candidates.append(rotation)
                candidates.append(np.vstack((-x, -y, z)))
    return candidates


def build_representation(point_group: PointGroup, positions: np.ndarray, elements: list[str], tolerance: float) -> tuple[RepresentationSymb, list[list[int]]]:
    if len(positions) == 0:
        fail("The basis site list is empty.", "empty_basis")
    if tolerance < 0:
        fail("symmetryTolerance must be non-negative.", "invalid_tolerance")

    matrices = []
    permutations = []
    for element in point_group.elements:
        rotated = (np.asarray(element.matrix_num, dtype=float) @ positions.T).T
        permutation = []
        for source_index, rotated_position in enumerate(rotated):
            distances = np.linalg.norm(positions - rotated_position, axis=1)
            matches = [index for index, distance in enumerate(distances)
                       if (not elements[source_index] or not elements[index] or
                           elements[index] == elements[source_index]) and distance <= tolerance]
            if len(matches) != 1:
                fail(f"Group element {source_index} does not map the basis uniquely.", "basis_not_closed")
            permutation.append(matches[0])
        matrix = sp.zeros(len(positions))
        for source_index, target_index in enumerate(permutation):
            matrix[source_index, target_index] = 1
        matrices.append(matrix)
        permutations.append(permutation)
    return RepresentationSymb(matrices, point_group=point_group), permutations


def reduce_representation(point_group: PointGroup, positions: np.ndarray, elements: list[str], labels: list[str], tolerance: float) -> tuple[dict, list[list[int]]]:
    representation, permutations = build_representation(point_group, positions, elements, tolerance)
    reduction = point_group.ireps.reduce_repr(representation)
    decomposition = []
    projected_vectors = []
    for irrep_label, multiplicity in reduction.items():
        multiplicity = int(multiplicity)
        if multiplicity <= 0:
            continue
        irrep = point_group.ireps[irrep_label].get_repr()
        dimension = int(irrep.shape[-1])
        decomposition.append({"irrepLabel": irrep_label, "multiplicity": multiplicity, "dimension": dimension})
        for row in range(dimension):
            occurrence_index = 0
            projector = irrep.generalized_projection_operator(representation, row, row)
            _, pivots = projector.rref()
            vectors = GramSchmidt([projector.col(column) for column in pivots], orthonormal=True)
            for vector in vectors:
                projected_vectors.append({
                    "irrepLabel": irrep_label,
                    "occurrenceIndex": occurrence_index,
                    "irrepRow": row,
                    "coefficients": [coefficient_payload(vector[index]) for index in range(len(labels))],
                })
                occurrence_index += 1
    return {"decomposition": decomposition, "projectedVectors": projected_vectors}, permutations


def detect_group(elements: list[str], positions: np.ndarray, tolerance: float) -> tuple[str, str, PointGroupAnalyzer]:
    analyzer = PointGroupAnalyzer(Molecule(elements, positions), tolerance=tolerance)
    symbol = str(analyzer.sch_symbol)
    if "*" in symbol or "inf" in symbol.lower():
        return "", symbol, analyzer
    return symbol, symbol, analyzer


def analyze(payload: dict) -> dict:
    sites = payload.get("sites", [])
    if not sites:
        fail("The basis site list is empty.", "empty_basis")
    tolerance = float(payload.get("symmetryTolerance", 0.1))
    elements = [str(site.get("element", "")) for site in sites]
    labels = [str(site.get("label", "")) for site in sites]
    positions = np.asarray([site["position"] for site in sites], dtype=float)
    analyzer = None
    requested = str(payload.get("pointGroupLabel", ""))
    if requested:
        label = requested
        detection = {"ran": False, "determined": True, "pointGroupLabel": label,
                     "detectorSymbol": "", "tolerance": tolerance, "reason": ""}
    else:
        label, symbol, analyzer = detect_group(elements, positions, tolerance)
        detection = {"ran": True, "determined": bool(label), "pointGroupLabel": label,
                     "detectorSymbol": symbol, "tolerance": tolerance,
                     "reason": "Linear point groups are not represented by a finite group." if not label else ""}
        if not label:
            return {"detection": detection}

    try:
        point_group = PointGroup(label)
    except Exception as exc:
        fail(str(exc), "unknown_point_group")

    if analyzer is None:
        analyzer = PointGroupAnalyzer(Molecule(elements, positions), tolerance=tolerance)
    rotation = np.eye(3)
    aligned = False
    for candidate in frame_candidates(positions, analyzer):
        transformed = (candidate @ positions.T).T
        try:
            build_representation(point_group, transformed, elements, tolerance)
            rotation = candidate
            positions = transformed
            aligned = True
            break
        except AnalysisError:
            continue
    if not aligned:
        fail("No proper rotation aligned the selected sites with the point-group frame.", "frame_alignment_failed")

    reduction, permutations = reduce_representation(point_group, positions, elements, labels, tolerance)
    class_labels = [str(name) for name in point_group.class_names]
    class_sizes = [int(size) for size in list(point_group.ireps.class_sizes)]
    irrep_labels = list(point_group.ireps.keys())
    characters = []
    for irrep_label in irrep_labels:
        values = list(point_group.ireps[irrep_label].values)
        characters.append([coefficient_payload(value) for value in values])
    reducible = []
    for group_class in point_group.classes:
        representative = point_group.elements.index(group_class.representative)
        permutation = permutations[representative]
        reducible.append(coefficient_payload(sum(1 for index, target in enumerate(permutation) if index == target)))

    multiplets = []
    total_states = 0
    active = payload.get("activeOrbitalIrreps", [])
    electron_count = int(payload.get("activeElectronCount", 0))
    if active and electron_count:
        try:
            terms = ActiveSpace.from_orbitals(point_group, active, nel=electron_count).term_table()
            for term in terms.entries:
                irrep, spin, dimension, count, total = term
                entry = {"irrepLabel": str(irrep), "spinMultiplicity": int(2 * spin + 1),
                         "irrepDimension": int(dimension), "countPerRow": int(count), "totalStates": int(total)}
                multiplets.append(entry)
                total_states += int(total)
        except Exception as exc:
            fail(str(exc), "invalid_active_space")

    return {
        "detection": detection,
        "frameRotation": rotation.tolist(),
        "characterTable": {"pointGroupLabel": label, "groupOrder": len(point_group.elements),
                            "classLabels": class_labels, "classSizes": class_sizes,
                            "irrepLabels": irrep_labels,
                            "irrepDimensions": [int(point_group.ireps[name].get_repr().shape[-1]) for name in irrep_labels],
                            "characters": characters},
        "reducibleCharacters": reducible,
        "reduction": {"pointGroupLabel": label, "siteLabels": labels,
                       "groupOrder": len(point_group.elements), **reduction},
        "multiplets": multiplets,
        "multipletTotalStates": total_states,
    }


def main() -> int:
    if len(sys.argv) < 2:
        print(json.dumps({"error": "missing_payload", "detail": "Expected a payload JSON path."}), file=sys.stderr)
        return 1
    try:
        with open(sys.argv[1], "r", encoding="utf-8") as handle:
            payload = json.load(handle)
        print(json.dumps(analyze(payload), separators=(",", ":")))
        return 0
    except AnalysisError as exc:
        print(json.dumps({"error": exc.code, "detail": exc.detail}), file=sys.stderr)
        return 1
    except Exception as exc:
        print(json.dumps({"error": "analysis_failed", "detail": str(exc)}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
