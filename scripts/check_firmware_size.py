#!/usr/bin/env python3
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUDGET_PATH = ROOT / "scripts" / "firmware_size_budget.json"
ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
SIZE_RE = {
    "ram_bytes": re.compile(r"RAM:.*\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)"),
    "flash_bytes": re.compile(r"Flash:.*\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)"),
}


def load_budget() -> dict:
    with BUDGET_PATH.open("r", encoding="utf-8") as handle:
        budget = json.load(handle)

    for resource in ("ram_bytes", "flash_bytes"):
        expected_limit = budget["baseline"][resource] + budget["headroom"][resource]
        if budget["limits"][resource] != expected_limit:
            raise ValueError(
                f"invalid {resource} budget: limit must equal baseline + headroom"
            )
    return budget


def run_size(environment: str) -> str:
    command = ["pio", "run", "-e", environment, "-t", "size"]
    print("+", " ".join(command), flush=True)
    completed = subprocess.run(
        command,
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    print(completed.stdout, end="")
    if completed.returncode != 0:
        raise RuntimeError(f"PlatformIO size command failed with exit code {completed.returncode}")
    return ANSI_RE.sub("", completed.stdout)


def parse_size(output: str) -> dict:
    result = {}
    totals = {}
    for resource, pattern in SIZE_RE.items():
        match = pattern.search(output)
        if not match:
            raise ValueError(f"could not parse {resource} from PlatformIO output")
        result[resource] = int(match.group(1))
        totals[resource] = int(match.group(2))
    return {"used": result, "totals": totals}


def main() -> int:
    try:
        budget = load_budget()
        measured = parse_size(run_size(budget["environment"]))
    except (OSError, KeyError, TypeError, ValueError, RuntimeError) as exc:
        print(f"firmware size check failed: {exc}", file=sys.stderr)
        return 2

    failed = False
    for resource, label in (("ram_bytes", "RAM"), ("flash_bytes", "Flash")):
        used = measured["used"][resource]
        total = measured["totals"][resource]
        baseline = budget["baseline"][resource]
        limit = budget["limits"][resource]
        expected_total = budget["totals"][resource]

        if total != expected_total:
            print(
                f"{label}: total changed from budget {expected_total} to {total}; "
                "review the board/partition configuration before updating the budget",
                file=sys.stderr,
            )
            failed = True
            continue

        delta = used - baseline
        remaining = limit - used
        print(
            f"{label}: used={used} baseline={baseline} delta={delta:+d} "
            f"limit={limit} remaining={remaining}"
        )
        if used > limit:
            print(
                f"{label}: resource budget exceeded by {used - limit} bytes",
                file=sys.stderr,
            )
            failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
