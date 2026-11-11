import re
import sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
header = (root / "src" / "shared" / "protocol.h").read_text(encoding="utf-8")
m = re.search(r'Version\s*=\s*"([^"]+)"', header)
if not m:
    sys.exit("version not found in protocol.h")
sys.stdout.write(m.group(1))
