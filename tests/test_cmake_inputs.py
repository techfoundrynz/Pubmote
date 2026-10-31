"""Prevent stale PlatformIO source graphs after component/manifest changes."""
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from cmake_inputs import CACHE_VARIABLE, cmake_input_fingerprint, invalidate_cmake_cache, invalidate_bootloader_cache


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

    def test_sdk_change_refreshes_bootloader_even_after_app_reconfigured(self):
        sdkconfig = self.root / "sdkconfig.test"
        sdkconfig.write_text("CONFIG_ESPTOOLPY_FLASHMODE_DIO=y\n")
        self.assertFalse(invalidate_bootloader_cache(self.build, sdkconfig))
        cache = self.build / "bootloader" / "CMakeCache.txt"
        replies = cache.parent / ".cmake/api/v1/reply"
        replies.mkdir(parents=True)
        cache.write_text("old bootloader configuration")
        reply = replies / "codemodel-v2-old.json"
        reply.write_text("{}")
        memory = self.build / "memory.ld"
        memory.write_text("rtc_slow_seg: old SDK layout")
        self.record()  # The app model can already reflect the new SDK settings.
        self.assertFalse(invalidate_bootloader_cache(self.build, sdkconfig))
        self.assertTrue(reply.exists())
        self.assertTrue(memory.exists())
        before = sdkconfig.stat()
        sdkconfig.write_text("CONFIG_ESPTOOLPY_FLASHMODE_QIO=y\n")
        os.utime(sdkconfig, ns=(before.st_atime_ns, before.st_mtime_ns))
        self.assertTrue(invalidate_bootloader_cache(self.build, sdkconfig))
        self.assertFalse(cache.exists())
        self.assertFalse(reply.exists())
        self.assertFalse(memory.exists())
        self.assertTrue(self.cache.exists())

    def test_missing_bootloader_cache_also_invalidates_its_api_reply(self):
        sdkconfig = self.root / "sdkconfig.test"
        sdkconfig.write_text("CONFIG_ESPTOOLPY_FLASHMODE_QIO=y\n")
        self.assertFalse(invalidate_bootloader_cache(self.build, sdkconfig))
        replies = self.build / "bootloader/.cmake/api/v1/reply"
        replies.mkdir(parents=True)
        reply = replies / "codemodel-v2-stale.json"
        reply.write_text("{}")
        self.assertTrue(invalidate_bootloader_cache(self.build, sdkconfig))
        self.assertFalse(reply.exists())

    def test_missing_sdkconfig_does_not_touch_build_artifacts(self):
        self.record()
        self.assertFalse(invalidate_bootloader_cache(self.build, self.root / "missing"))
        self.assertTrue(self.cache.exists())


if __name__ == "__main__":
    unittest.main()
