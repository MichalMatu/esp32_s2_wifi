import subprocess
from pathlib import Path

Import("env")  # type: ignore[name-defined]

ROOT = Path(env["PROJECT_DIR"])  # type: ignore[name-defined]
WEB = ROOT / "web"


def run(command):
    subprocess.run(command, cwd=WEB, check=True)


if WEB.exists():
    if not (WEB / "node_modules").exists():
        if (WEB / "package-lock.json").exists():
            run(["npm", "ci"])
        else:
            run(["npm", "install"])

    run(["npm", "run", "build"])
