from pathlib import Path
import sys

prefix = Path(sys.argv[1])
roots = (prefix / "share" / "iiSDKBuilder", prefix / "lib" / "cmake" / "iiSDKBuilder")
if any(not root.is_dir() for root in roots):
    raise SystemExit("Installed iiSDKBuilder package is incomplete")
metadata = [str(path.relative_to(prefix)) for root in roots for path in root.rglob("*")
            if path.name.endswith(".iisacc-preview")
            or path.name in {".iisacc-temporary.lock", ".DS_Store"}]
if metadata:
    raise SystemExit("Installed private provider metadata: " + ", ".join(sorted(metadata)))
print("Installed iiSDKBuilder package excludes provider-private metadata")
