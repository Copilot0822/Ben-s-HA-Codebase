"""Copy project sources to a Linux path for the official esp-matter toolchain."""
from pathlib import Path
import shutil
import sys

source, destination = (Path(argument).resolve() for argument in sys.argv[1:])
if source == destination or source in destination.parents:
    raise SystemExit("Build staging directory must be outside the repository source")
if " " in str(destination) or "'" in str(destination):
    raise SystemExit("Use a Linux staging directory without spaces or apostrophes")
destination.mkdir(parents=True, exist_ok=True)
excluded = {"build", "managed_components", "artifacts", ".git", "__pycache__", "sdkconfig", "sdkconfig.old"}
for path in source.rglob("*"):
    relative = path.relative_to(source)
    if any(part in excluded for part in relative.parts) or path.suffix == ".log":
        continue
    target = destination / relative
    if path.is_dir():
        target.mkdir(parents=True, exist_ok=True)
    elif path.is_file():
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or path.read_bytes() != target.read_bytes():
            shutil.copy2(path, target)
print(f"Staged source: {destination}")
