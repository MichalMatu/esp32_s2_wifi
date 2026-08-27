import subprocess
from pathlib import Path

Import("env")  # type: ignore[name-defined]

ROOT = Path(env["PROJECT_DIR"])  # type: ignore[name-defined]
WEB = ROOT / "web"
LOCKFILE = WEB / "package-lock.json"


def run(command):
    subprocess.run(command, cwd=WEB, check=True)


if WEB.exists():
    if not LOCKFILE.exists():
        raise RuntimeError("web/package-lock.json is required for reproducible firmware builds")

    if not (WEB / "node_modules").exists():
        run(["npm", "ci"])

    run(["npm", "run", "build"])
