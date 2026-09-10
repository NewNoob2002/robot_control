#!/usr/bin/env python3
"""Validate the live frame guard without importing or executing either hardware runner."""
import ast
from pathlib import Path

source = ast.parse((Path(__file__).parent / "local_trial.py").read_text())
guard = next(node for node in source.body if isinstance(node, ast.FunctionDef) and node.name == "validate_frame")
namespace = dict(go=True, frames=0, downloads=0, uploads=0, nmts=0, nonzero=0, terminal=0)
exec(compile(ast.Module(body=[guard], type_ignores=[]), "guard", "exec"), namespace)


def check(payload, accepted=True, identifier=0x601, **flags):
    """Check one exact candidate frame while restoring counters after deliberate rejection."""
    counters = {key: namespace[key] for key in ("frames", "downloads", "uploads", "nmts", "nonzero", "terminal")}
    frame = {"can_id": identifier, "data_hex": payload, **flags}
    try:
        namespace["validate_frame"](frame)
    except (AssertionError, RuntimeError):
        assert not accepted, frame
        namespace.update(counters)
    else:
        assert accepted, frame


check("2B002000E8030000")
check("4000200000000000")
check("2B002000E7030000", False)
check("2B00180500000000", False)
check("2B40600002000000", False)
check("2B002000E8030000", False, fd=True)
check("2B002000E8030000", False, extended=True)
check("", identifier=0x281)
check("2360FF0205000000", False)  # Swapped index bytes.
check("23FF600205000000")
check("23FF600205000000", False)  # Non-renewable target.
check("2B4060000F000000", False)  # No re-enable after motion.
check("23FF600100000000")
check("23FF600200000000")
check("2B40600006000000")
check("2B40600006000000", False)  # No repeated terminal controlword.
check("2B00200000000000")
print("PASS: watchdog frame guard, one target, no re-enable, no TPDO write, exact watchdog values")
