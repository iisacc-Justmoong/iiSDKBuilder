import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("native_kit", Path(__file__).parents[1] / "tools/build_native_kit.py")
kit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kit)


class NativeKit(unittest.TestCase):
    def test_manifest(self):
        records = kit.manifest()["dependencies"]
        self.assertEqual(len({p["name"] for p in records}), 5)
        for record in records:
            self.assertRegex(record["sha256"], r"^[a-f0-9]{64}$")
            self.assertTrue(record["url"].startswith("https://"))

    def test_archive_escape(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parents[1] / "build") as root:
            root = Path(root)
            for name in ("../../escape", "/absolute-escape"):
                archive = root / "bad.tar"
                with tarfile.open(archive, "w") as target:
                    member = tarfile.TarInfo(name)
                    member.size = 1
                    target.addfile(member, io.BytesIO(b"x"))
                with self.assertRaises(ValueError):
                    kit.unpack(archive, root / "output")
            self.assertFalse((root / "escape").exists())


if __name__ == "__main__":
    unittest.main()
