"""Restricted ZPL arithmetic over named final TOTEN energies (eV)."""
import ast
import math
import operator

VARIABLES = ("E_gs", "E_es", "E_T", "E_es@gs", "E_gs@es")
PRESETS = {"dscf": "E_es - E_gs", "spin_purified": "2*E_es - E_T - E_gs"}
_NAMES = {name.replace("@", "_at_"): name for name in VARIABLES}
_DIRECTORIES = dict(zip(VARIABLES, ("stanu podstawowego", "stanu wzbudzonego", "trypletu",
                                   "stanu wzbudzonego w geometrii podstawowej",
                                   "stanu podstawowego w geometrii wzbudzonej")))
_OPERATIONS = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
               ast.Div: operator.truediv, ast.UAdd: operator.pos, ast.USub: operator.neg}


def _parse(formula):
    try:
        tree = ast.parse(formula.replace("@", "_at_").strip(), mode="eval")
    except (SyntaxError, ValueError) as exc:
        raise ValueError(f"Niedozwolony element we wzorze ZPL: {exc}") from exc
    for node in ast.walk(tree):
        if type(node) not in (ast.Expression, ast.BinOp, ast.UnaryOp, ast.Constant, ast.Name,
                              ast.Load, *_OPERATIONS):
            raise ValueError(f"Niedozwolony element we wzorze ZPL: {type(node).__name__}")
        if isinstance(node, ast.Constant) and type(node.value) not in (int, float):
            raise ValueError(f"Niedozwolony element we wzorze ZPL: {node.value!r}")
        if isinstance(node, ast.Name) and node.id not in _NAMES:
            raise ValueError(f"Nieznana nazwa we wzorze ZPL: {node.id}; dozwolone: {', '.join(VARIABLES)}")
    return tree


def used_variables(formula) -> list[str]:
    names = {_NAMES[node.id] for node in ast.walk(_parse(formula)) if isinstance(node, ast.Name)}
    return [name for name in VARIABLES if name in names]


def evaluate(formula, energies) -> float:
    tree = _parse(formula)

    def value(node):
        if isinstance(node, ast.Constant):
            return node.value
        if isinstance(node, ast.Name):
            name = _NAMES[node.id]
            energy = energies.get(name)
            if energy is None:
                raise ValueError(f"Wzór używa {name}, ale nie podano katalogu {_DIRECTORIES[name]}")
            return energy
        if isinstance(node, ast.UnaryOp):
            return _OPERATIONS[type(node.op)](value(node.operand))
        return _OPERATIONS[type(node.op)](value(node.left), value(node.right))

    try:
        result = float(value(tree.body))
    except (ArithmeticError, TypeError) as exc:
        raise ValueError(f"Nieprawidłowy wynik wzoru ZPL: {exc}") from exc
    if not math.isfinite(result) or result <= 0:
        raise ValueError("Wynik wzoru ZPL musi być skończony i dodatni (eV)")
    return result
