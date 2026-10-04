from __future__ import annotations

import json
import sys

try:
    import numpy as np
    import sympy as sp
    from pymatgen.core import Molecule
    from pymatgen.symmetry.analyzer import PointGroupAnalyzer
    from groupy import PointGroup
    from groupy.multiplets.terms import ActiveSpace
    from salc_basis import SalcError, coefficient_payload, symmetry_adapted_basis
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


def normalize(vector: np.ndarray) -> np.ndarray:
    length = np.linalg.norm(vector)
    return vector / length if length > 1e-12 else vector


def frame_candidates(positions: np.ndarray, analyzer: PointGroupAnalyzer) -> list[np.ndarray]:
    # The app sends the sites in the user's defect axes when it has them (z = defect axis), so the
    # input frame itself, then any frame that keeps its z, go first: the result then refers to the
    # user's z, and to the user's x too when the group allows it.
    candidates = [np.eye(3)]
    axes = [np.array([0.0, 0.0, 1.0])]
    for axis in getattr(analyzer, "rot_sym", []):
        axes.append(normalize(np.asarray(axis[0], dtype=float)))
    principal = np.asarray(getattr(analyzer, "principal_axes", np.eye(3)), dtype=float)
    axes.extend(normalize(principal[index]) for index in range(3))
    axes.extend(normalize(principal[:, index]) for index in range(3))
    axes.extend(normalize(positions[index]) for index in range(len(positions)))

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


def build_representation(point_group: PointGroup, positions: np.ndarray, elements: list[str], tolerance: float) -> list[list[int]]:
    if len(positions) == 0:
        fail("The basis site list is empty.", "empty_basis")
    if tolerance < 0:
        fail("symmetryTolerance must be non-negative.", "invalid_tolerance")

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
        permutations.append(permutation)
    return permutations


def reduce_representation(point_group: PointGroup, positions: np.ndarray, elements: list[str], labels: list[str], tolerance: float) -> tuple[dict, list[list[int]]]:
    permutations = build_representation(point_group, positions, elements, tolerance)
    return symmetry_adapted_basis(point_group, permutations), permutations


def active_shells(orbitals: list[str], space: ActiveSpace) -> list[dict]:
    shells = []
    first = 0
    occurrences: dict[str, int] = {}
    for irrep in orbitals:
        dimension = int(point_group_dimension(space, first))
        occurrence = occurrences.get(irrep, 0)
        occurrences[irrep] = occurrence + 1
        if dimension == 1:
            shell_label = space.orbital_names[first]
        else:
            import re
            match = re.match(r"([A-Za-z])(.*)", irrep)
            letter, rest = (match.group(1).lower(), match.group(2).lower()) if match else (irrep.lower(), "")
            shell_label = letter + rest + ("'" * occurrence)
            if rest:
                shell_label = letter + "_{" + rest + "}" + ("'" * occurrence)
        shells.append({"irrepLabel": irrep, "label": shell_label, "firstOrbital": first, "dimension": dimension})
        first += dimension
    return shells


def point_group_dimension(space: ActiveSpace, orbital_index: int) -> int:
    for representation in space.orbital_reps:
        dimension = int(representation.shape[-1])
        if orbital_index < dimension:
            return dimension
        orbital_index -= dimension
    return 1


def wavefunction_payload(space: ActiveSpace, orbitals: list[str], terms) -> tuple[list[dict], list[dict]]:
    shells = active_shells(orbitals, space)
    states = []
    for irrep, spin, dimension, count, _ in terms.entries:
        for row in range(int(dimension)):
            for twice_ms in range(int(2 * spin), int(-2 * spin) - 1, -2):
                for copy_index, state in enumerate(space.terms(str(irrep), spin, sp.Rational(twice_ms, 2), row, numeric=False)):
                    determinants = []
                    first_configuration = None
                    for coefficient, determinant in state.nonzero_terms():
                        occupied = [{"orbitalIndex": int(j) // 2, "spinUp": int(j) % 2 == 0}
                                    for j in determinant.occupied_indices]
                        if first_configuration is None:
                            first_configuration = [sum(1 for item in occupied if shell["firstOrbital"] <= item["orbitalIndex"] < shell["firstOrbital"] + shell["dimension"])
                                                   for shell in shells]
                        determinants.append({"coefficient": coefficient_payload(coefficient), "occupied": occupied})
                    if first_configuration is not None:
                        states.append({"irrepLabel": str(irrep), "spinMultiplicity": int(2 * spin + 1),
                                       "copyIndex": copy_index, "irrepRow": row, "twiceMs": twice_ms,
                                       "configuration": first_configuration, "determinants": determinants})
    return states, shells


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
    class_sizes = [int(group_class.size) for group_class in point_group.classes]
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

    tensor_power = 0
    tensor_power_decomposition = []
    electron_count = int(payload.get("activeElectronCount", 0))
    if electron_count > 0:
        identity_character = int(reducible[class_labels.index("E")]["numeric"])
        if identity_character ** electron_count <= 2**31 - 1:
            tensor_power = electron_count
            class_characters = [sp.sympify(value["exact"]) for value in reducible]
            group_order = len(point_group.elements)
            for irrep_label in irrep_labels:
                characters_for_irrep = [sp.sympify(value["exact"]) for value in characters[irrep_labels.index(irrep_label)]]
                multiplicity = sum(
                    class_size * sp.conjugate(irrep_character) * reducible_character ** electron_count
                    for class_size, irrep_character, reducible_character in zip(
                        class_sizes, characters_for_irrep, class_characters)) / group_order
                multiplicity = int(sp.simplify(multiplicity))
                if multiplicity > 0:
                    tensor_power_decomposition.append({
                        "irrepLabel": irrep_label,
                        "multiplicity": multiplicity,
                        "dimension": int(point_group.ireps[irrep_label].get_repr().shape[-1]),
                    })

    multiplets = []
    total_states = 0
    active_shell_result = []
    active_orbital_labels_result = []
    wavefunctions = []
    wavefunctions_skipped_reason = ""
    active = payload.get("activeOrbitalIrreps", [])
    if active and electron_count:
        try:
            automatic_space = ActiveSpace.from_orbitals(point_group, active, nel=electron_count)
            automatic_labels = list(automatic_space.orbital_names)
            requested_labels = [str(value) for value in payload.get("activeOrbitalLabels", [])]
            if requested_labels and len(requested_labels) != len(automatic_labels):
                fail("activeOrbitalLabels must contain one label per spatial orbital.", "invalid_active_space")
            final_labels = [requested if requested else automatic
                            for requested, automatic in zip(requested_labels, automatic_labels)] if requested_labels else automatic_labels
            space = ActiveSpace.from_orbitals(point_group, active, nel=electron_count,
                                              labels=final_labels if requested_labels else None)
            active_orbital_labels_result = list(space.orbital_names)
            terms = space.term_table()
            for term in terms.entries:
                irrep, spin, dimension, count, total = term
                entry = {"irrepLabel": str(irrep), "spinMultiplicity": int(2 * spin + 1),
                         "irrepDimension": int(dimension), "countPerRow": int(count), "totalStates": int(total)}
                multiplets.append(entry)
                total_states += int(total)
            wavefunction_dimension = space.dim
            active_shell_result = active_shells([str(value) for value in active], space)
            if wavefunction_dimension <= 1000:
                wavefunctions, _ = wavefunction_payload(space, [str(value) for value in active], terms)
            else:
                wavefunctions_skipped_reason = f"Slater basis too large (D={wavefunction_dimension})"
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
        "tensorPower": tensor_power,
        "tensorPowerDecomposition": tensor_power_decomposition,
        "activeShells": active_shell_result,
        "activeOrbitalLabels": active_orbital_labels_result,
        "wavefunctions": wavefunctions,
        "wavefunctionsSkippedReason": wavefunctions_skipped_reason,
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
    except (AnalysisError, SalcError) as exc:
        print(json.dumps({"error": exc.code, "detail": exc.detail}), file=sys.stderr)
        return 1
    except Exception as exc:
        print(json.dumps({"error": "analysis_failed", "detail": str(exc)}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
