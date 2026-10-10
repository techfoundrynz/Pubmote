"""Prevent stale PlatformIO source graphs after component/manifest changes."""
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from cmake_inputs import CACHE_VARIABLE, cmake_input_fingerprint, invalidate_cmake_cache


class CMakeInputsTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / "CMakeLists.txt").write_text("project(test)", encoding="utf-8")
        self.module = self.root / "firmware/modules/support/CMakeLists.txt"
        self.module.parent.mkdir(parents=True)
        self.module.write_text('SRCS "src/string_utils.c"', encoding="utf-8")
        self.build = self.root / ".pio/build/test"
        self.build.mkdir(parents=True)
        self.cache = self.build / "CMakeCache.txt"

    def fingerprint(self, args="-DSLINT_CPP_FILES=8"):
        return cmake_input_fingerprint(self.root, args)

    def record(self):
        self.cache.write_text(f"{CACHE_VARIABLE}:STRING={self.fingerprint()}\n", encoding="utf-8")

    def test_legacy_cache_is_invalidated(self):
        self.cache.write_text("CMAKE_HOME_DIRECTORY:INTERNAL=project\n", encoding="utf-8")
        self.assertTrue(invalidate_cmake_cache(self.build, self.fingerprint()))
        self.assertFalse(self.cache.exists())

    def test_unchanged_definitions_keep_cache(self):
        self.record()
        self.assertFalse(invalidate_cmake_cache(self.build, self.fingerprint()))
        self.assertTrue(self.cache.exists())

    def test_removed_source_entry_invalidates_even_with_preserved_timestamp(self):
        self.record()
        previous = self.module.stat()
        self.module.write_text("SRCS", encoding="utf-8")
        os.utime(self.module, ns=(previous.st_atime_ns, previous.st_mtime_ns))
        self.assertTrue(invalidate_cmake_cache(self.build, self.fingerprint()))

    def test_added_and_deleted_manifests_change_fingerprint(self):
        initial = self.fingerprint()
        manifest = self.module.parent / "sources.cmake"
        manifest.write_text("set(sources new.c)", encoding="utf-8")
        self.assertNotEqual(initial, self.fingerprint())
        self.record()
        manifest.unlink()
        self.assertTrue(invalidate_cmake_cache(self.build, self.fingerprint()))

    def test_arguments_change_fingerprint(self):
        self.record()
        self.assertTrue(invalidate_cmake_cache(self.build, self.fingerprint("-DSLINT_CPP_FILES=4")))

    def test_non_build_files_do_not_invalidate(self):
        self.record()
        (self.module.parent / "README.md").write_text("Updated documentation", encoding="utf-8")
        (self.module.parent / "codec.c").write_text("void codec(void) {}", encoding="utf-8")
        self.assertFalse(invalidate_cmake_cache(self.build, self.fingerprint()))

    def test_fresh_build_needs_no_invalidation(self):
        self.assertFalse(invalidate_cmake_cache(self.build, self.fingerprint()))


if __name__ == "__main__":
    unittest.main()
