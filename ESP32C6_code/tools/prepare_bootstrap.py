"""Use official SDK package pins without unrelated host Python test packages."""
import json
from pathlib import Path
import sys

chip = Path(sys.argv[1]).resolve()
config = json.loads((chip / "scripts/setup/environment.json").read_text())
config.pop("virtualenv", None)
(chip / ".environment/sdp810-tools.json").write_text(json.dumps(config, indent=2) + "\n")
