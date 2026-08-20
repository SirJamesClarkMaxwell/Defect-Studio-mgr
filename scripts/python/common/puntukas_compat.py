"""Local workarounds for puntukas quirks that affect our bridge scripts. Fixed here rather than
upstream in punktukas-tools (same policy as the Bohr/Angstrom unit bug in
scripts/python/examples/puntukas_structure_load.py)."""


def patch_incar_tolerant_encoding() -> None:
    """puntukas.vasp.incar.incar.Incar.init_from_file() opens INCAR with the platform default
    encoding (`open(filepath, "rt")`, no encoding kwarg). Real INCAR files sometimes carry a
    stray non-UTF-8 byte in a comment (observed: 0xc3 with no valid continuation byte), which
    raises UnicodeDecodeError and aborts the whole VaspOutput.from_directory() call before we
    ever reach the WAVECAR/vasprun.xml data we actually want. INCAR's own tag values are pure
    ASCII, so tolerating/garbling a stray byte in a comment is harmless - replace it rather than
    fail the load."""
    import puntukas.vasp.incar.incar as incar_module

    original_open = open

    def _tolerant_open(file, mode="r", *args, **kwargs):
        if "b" not in mode and "encoding" not in kwargs:
            kwargs.setdefault("encoding", "utf-8")
            kwargs.setdefault("errors", "replace")
        return original_open(file, mode, *args, **kwargs)

    incar_module.open = _tolerant_open
