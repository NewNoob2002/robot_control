#!/usr/bin/env python3
"""Select CI suites from trusted event metadata and the complete Git diff."""

import json
import os
from pathlib import Path
import re
import subprocess


SUITES = {"full", "sbus", "phase6", "runtime"}
RUNTIME_PATHS = (
    "application/control/", "tests/unit/control_cycle_tests.cpp", "docs/verification/evidence/p10_1_",
    "domain/drive/runtime.", "communication/canopen/runtime.",
    "tests/unit/canopen_runtime_", "docs/verification/evidence/p8r_",
)
SBUS_PATHS = (
    "input/sbus/", "tools/sbus_observer/", "platform/linux/uart/",
    "tests/unit/sbus_", "tests/integration/sbus_", "scripts/test/test_sbus_",
    "docs/verification/evidence/p9_",
)
CAN_PATHS = (
    "communication/canopen/", "platform/linux/can/", "tools/can_probe/",
    "tools/canopen_", "tools/zlac_qualification/", "tools/can_interface_inhibitor/",
    "tests/unit/canopen_", "tests/unit/socketcan_", "tests/unit/can_interface_",
    "scripts/test/test_phase6_", "scripts/test/test_qualification_",
    "docs/verification/evidence/p6_",
)


def classify(paths):
    """Return affected suites; unclassified non-documentation changes run all."""
    suites = set()
    for path in paths:
        if path.startswith(RUNTIME_PATHS):
            suites.add("runtime")
        elif path.startswith(SBUS_PATHS):
            suites.add("sbus")
        elif path.startswith(CAN_PATHS):
            suites.add("phase6")
            if path.startswith(("communication/canopen/", "platform/linux/can/")):
                suites.add("runtime")
        elif path in ("README.md", "AGENTS.md") or (
            path.startswith("docs/") and path.endswith(".md")
        ):
            continue
        else:
            return set(SUITES)
    return suites


def select(event_name, event, ref):
    """Select suites for push/PR; missing history and integration events run all."""
    if event_name == "push" and ref != "refs/heads/main":
        before, after = event.get("before", ""), event.get("after", "")
        three_dot = False
    elif event_name == "pull_request":
        pr = event["pull_request"]
        if pr["base"]["ref"] == "main":
            return set(SUITES)
        before, after = pr["base"]["sha"], pr["head"]["sha"]
        three_dot = True
    else:
        return set(SUITES)
    if any(not re.fullmatch(r"[0-9a-f]{40}", sha) or sha == "0" * 40
           for sha in (before, after)):
        return set(SUITES)
    revisions = [before + "..." + after] if three_dot else [before, after]
    try:
        # Disable rename detection so both the old and new scope are tested.
        result = subprocess.run(
            ["git", "diff", "--name-only", "--no-renames", "-z", *revisions, "--"],
            check=True, capture_output=True,
        )
    except subprocess.CalledProcessError:
        print("Base/head history unavailable; selecting full validation.")
        return set(SUITES)
    return classify(os.fsdecode(result.stdout).rstrip("\0").split("\0")
                    if result.stdout else [])


def main():
    """Write fixed boolean job outputs and a human-readable selection summary."""
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    suites = select(os.environ["GITHUB_EVENT_NAME"], event, os.environ["GITHUB_REF"])
    output = "".join(f"{name}={str(name in suites).lower()}\n" for name in sorted(SUITES))
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
        stream.write(output)
    print(output, end="")


if __name__ == "__main__":
    main()
