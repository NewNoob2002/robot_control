#!/usr/bin/env python3
"""Regression checks for CI routing without network or hardware access."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "select_scope", Path(__file__).resolve().parents[1] / "ci/select_scope.py"
)
scope = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scope)


def main():
    """Check scoped, mixed, fallback and real multi-commit/rename/delete diffs."""
    for paths, expected in (
        (["input/sbus/source/source.cpp"], {"sbus"}),
        (["platform/linux/uart/serial_port.cpp"], {"sbus"}),
        (["tests/unit/sbus_parser_tests.cpp", "README.md"], {"sbus"}),
        (["docs/verification/evidence/p9_2_sbus_manual_20260916/capture.gz"], {"sbus"}),
        (["communication/canopen/lifecycle.cpp"], {"phase6"}),
        (["platform/linux/can/socket.cpp"], {"phase6"}),
        (["scripts/test/test_phase6_evidence.py"], {"phase6"}),
        (["input/sbus/source/source.cpp", "tools/zlac_qualification/main.cpp"], {"sbus", "phase6"}),
        (["README.md", "docs/plans/PHASE9_SBUS_AND_INTEGRATION.md"], set()),
        ([], set()),
        (["domain/command/sample.hpp"], scope.SUITES),
        (["platform/linux/io/poll_wait.cpp"], scope.SUITES),
        (["CMakeLists.txt"], scope.SUITES),
        ([".github/workflows/ci.yml"], scope.SUITES),
        (["scripts/ci/select_scope.py"], scope.SUITES),
        (["future/new_module.cpp"], scope.SUITES),
    ):
        assert scope.classify(paths) == expected, paths
    for event in ("merge_group", "workflow_dispatch", "unknown"):
        assert scope.select(event, {}, "") == scope.SUITES
    assert scope.select("push", {}, "refs/heads/main") == scope.SUITES
    assert scope.select("push", {}, "refs/heads/codex/new") == scope.SUITES
    assert scope.select("pull_request", {"pull_request": {"base": {"ref": "main"}}}, "") == scope.SUITES
    with tempfile.TemporaryDirectory() as directory:
        def git(*args):
            """Run Git only in the isolated temporary test repository."""
            return subprocess.check_output(["git", "-C", directory, *args], text=True).strip()

        git("init", "-q")
        git("config", "user.name", "CI test")
        git("config", "user.email", "ci-test@example.invalid")
        root = Path(directory)
        (root / "README.md").write_text("initial\n")
        git("add", ".")
        git("commit", "-qm", "base")
        base = git("rev-parse", "HEAD")
        (root / "input/sbus").mkdir(parents=True)
        (root / "input/sbus/example.cpp").write_text("sample\n")
        git("add", ".")
        git("commit", "-qm", "sbus")
        (root / "README.md").write_text("docs followup\n")
        git("add", ".")
        git("commit", "-qm", "docs")
        head = git("rev-parse", "HEAD")
        original_run = subprocess.run

        def run_here(*args, **kwargs):
            """Keep the production diff call inside the isolated repository."""
            return original_run(*args, cwd=directory, **kwargs)

        with patch.object(scope.subprocess, "run", side_effect=run_here):
            assert scope.select("push", {"before": base, "after": head}, "refs/heads/codex/phase9") == {"sbus"}
            pr = {"pull_request": {"base": {"ref": "codex/phase9", "sha": base}, "head": {"sha": head}}}
            assert scope.select("pull_request", pr, "") == {"sbus"}
            assert scope.select("push", {"before": "0" * 40, "after": head}, "dev") == scope.SUITES
            assert scope.select("push", {"before": "1" * 40, "after": head}, "dev") == scope.SUITES
        (root / "communication/canopen").mkdir(parents=True)
        git("mv", "input/sbus/example.cpp", "communication/canopen/example.cpp")
        git("commit", "-qm", "rename across scopes")
        renamed = git("rev-parse", "HEAD")
        with patch.object(scope.subprocess, "run", side_effect=run_here):
            assert scope.select("push", {"before": head, "after": renamed}, "dev") == {"sbus", "phase6"}
        git("rm", "-q", "communication/canopen/example.cpp")
        git("commit", "-qm", "delete")
        deleted = git("rev-parse", "HEAD")
        with patch.object(scope.subprocess, "run", side_effect=run_here):
            assert scope.select("push", {"before": renamed, "after": deleted}, "dev") == {"phase6"}
        git("checkout", "-q", "--detach", base)
        (root / "shared.cpp").write_text("base-only change\n")
        git("add", ".")
        git("commit", "-qm", "base branch advanced")
        pr["pull_request"]["base"]["sha"] = git("rev-parse", "HEAD")
        with patch.object(scope.subprocess, "run", side_effect=run_here):
            assert scope.select("pull_request", pr, "") == {"sbus"}
    print("PASS: CI scope routing, event fallback, multi-commit, rename and deletion")


if __name__ == "__main__":
    main()
