import importlib.util
import tempfile
import unittest
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("freeze", ROOT / "tools/freeze_native_kit.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class InstalledKitContracts(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / "build")
        self.addCleanup(self.temporary.cleanup)
        self.prefix = Path(self.temporary.name) / "prefix"
        (self.prefix / "include").mkdir(parents=True)
        (self.prefix / "include/Native.h").write_text("#pragma once\n")

    def test_detects_changed_and_added_installed_files(self):
        MODULE.freeze(self.prefix)
        MODULE.freeze(self.prefix, True)
        (self.prefix / "include/Native.h").write_text("#pragma once\nstruct NewABI {};\n")
        with self.assertRaises(ValueError):
            MODULE.freeze(self.prefix, True)
        MODULE.freeze(self.prefix)
        (self.prefix / "include/Extra.h").write_text("#pragma once\n")
        with self.assertRaises(ValueError):
            MODULE.freeze(self.prefix, True)

    def test_rejects_transitive_qt_header_and_package(self):
        header = self.prefix / "include/Native.h"
        header.write_text("#include <QtCore/QObject>\n")
        with self.assertRaises(ValueError):
            MODULE.freeze(self.prefix)

        header.write_text("#pragma once\n")
        (self.prefix / "lib/cmake").mkdir(parents=True)
        (self.prefix / "lib/cmake/NativeConfig.cmake").write_text("find_dependency(Qt6 COMPONENTS Core)\n")
        with self.assertRaises(ValueError):
            MODULE.freeze(self.prefix)

    @unittest.skipIf(os.name=="nt","Symbolic link creation requires Windows developer mode")
    def test_rejects_dependency_links_outside_the_kit(self):
        link=self.prefix/"include/Alias.h"
        link.symlink_to("Native.h")
        MODULE.freeze(self.prefix)
        MODULE.freeze(self.prefix,True)
        link.unlink()
        (self.prefix.parent/"outside.h").write_text("#pragma once\n")
        link.symlink_to("../../outside.h")
        with self.assertRaises(ValueError):
            MODULE.freeze(self.prefix)



if __name__ == "__main__":
    unittest.main()
