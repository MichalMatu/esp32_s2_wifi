#!/usr/bin/env python3
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUDGET_PATH = ROOT / "scripts" / "firmware_size_budget.json"
ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
SIZE_RE = {
    "ram_bytes": re.compile(
        r"\bRAM:\s*.*?\(\s*used\s+([\d,]+)\s+bytes\s+from\s+([\d,]+)\s+bytes\s*\)",
        re.IGNORECASE,
    ),
    "flash_bytes": re.compile(
        r"\bFlash:\s*.*?\(\s*used\s+([\d,]+)\s+bytes\s+from\s+([\d,]+)\s+bytes\s*\)",
        re.IGNORECASE,
    ),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check ESP32-S2 static RAM and flash usage against the reviewed budget."
    )
    parser.add_argument(
        "--report",
        type=Path,
        help="Parse an existing PlatformIO build log instead of running another build.",
    )
    parser.add_argument(
        "--budget",
        type=Path,
        default=BUDGET_PATH,
        help="Budget JSON path (default: scripts/firmware_size_budget.json).",
    )
    return parser.parse_args()


def load_budget(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        budget = json.load(handle)

    for resource in ("ram_bytes", "flash_bytes"):
        expected_limit = budget["baseline"][resource] + budget["headroom"][resource]
        if budget["limits"][resource] != expected_limit:
            raise ValueError(
                f"invalid {resource} budget: limit must equal baseline + headroom"
            )
    return budget


def run_build(environment: str) -> str:
    command = ["pio", "run", "-e", environment]
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
        raise RuntimeError(
            f"PlatformIO build failed with exit code {completed.returncode}"
        )
    return completed.stdout


def read_report(report_path: Path | None, environment: str) -> str:
    if report_path is None:
        return run_build(environment)
    return report_path.read_text(encoding="utf-8", errors="replace")


def parse_size(output: str) -> dict:
    clean_output = ANSI_RE.sub("", output)
    used = {}
    totals = {}
    for resource, pattern in SIZE_RE.items():
        match = pattern.search(clean_output)
        if not match:
            raise ValueError(f"could not parse {resource} from PlatformIO build output")
        used[resource] = int(match.group(1).replace(",", ""))
        totals[resource] = int(match.group(2).replace(",", ""))
    return {"used": used, "totals": totals}


def check_budget(budget: dict, measured: dict) -> bool:
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

    return not failed


def main() -> int:
    args = parse_args()
    try:
        budget = load_budget(args.budget)
        output = read_report(args.report, budget["environment"])
        measured = parse_size(output)
    except (OSError, KeyError, TypeError, ValueError, RuntimeError) as exc:
        print(f"firmware size check failed: {exc}", file=sys.stderr)
        return 2

    return 0 if check_budget(budget, measured) else 1


if __name__ == "__main__":
    raise SystemExit(main())
