"""Prevent large-letter glyphs from being discarded by the MCU font plan."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from slint_fonts import font_settings


class FontPlanTests(unittest.TestCase):
    def test_wordmark_has_its_exact_size_and_letters_on_every_panel(self):
        for width in [240, 410, 466]:
            settings = font_settings(width / 240)
            size = int(54 * width / 240 + .5)
            self.assertIn(str(size), settings["SLINT_FONT_SIZES"].split(","))
            saira = settings["SLINT_FONT_PLAN"].split(";")[0].split("=", 1)[1]
            self.assertIn(f"{size}:pubmote ", saira.split("/"))

    @unittest.skipUnless(os.environ.get("SLINT_COMPILER"), "Set SLINT_COMPILER to verify actual embedded glyphs")
    def test_compiler_embeds_pixels_for_each_wordmark_letter(self):
        for width in [240, 410, 466]:
            with tempfile.TemporaryDirectory() as temporary:
                header = Path(temporary) / "app.h"
                subprocess.run([os.environ["SLINT_COMPILER"], str(ROOT / "firmware/src/slint/app-window.slint"),
                                "--embed-resources", "embed-for-software-renderer", "-o", str(header)],
                               env={**os.environ, **font_settings(width / 240)}, check=True, capture_output=True)
                source = header.read_text(encoding="utf-8")
                size = int(54 * width / 240 + .5)
                match = re.search(rf'\.pixel_size = {size}, \.glyph_data = slint::private_api::make_slice\((\w+)_glyphset_\d+', source)
                self.assertIsNotNone(match)
                resource = match[1]
                glyphset = re.search(rf'\.pixel_size = {size}, \.glyph_data = slint::private_api::make_slice\((\w+)', source)[1]
                entries = re.search(rf'{glyphset}\[\d+\] = \{{(.*?)\}};', source, re.S)[1]
                glyphs = re.findall(r'\{ \.x = .*?\}', entries)
                charmap = re.search(rf'{resource}_charmap\[\d+\] = \{{(.*?)\}};', source, re.S)[1]
                for letter in "pubmote":
                    index = int(re.search(rf'\.code_point = {ord(letter)}, \.glyph_index = (\d+)', charmap)[1])
                    self.assertNotIn("make_slice(nullptr, 0)", glyphs[index], (width, letter))
                    self.assertRegex(glyphs[index], r'\.width = [1-9]\d*', (width, letter))
                    self.assertRegex(glyphs[index], r'make_slice\(\w+, [1-9]\d*\)', (width, letter))


if __name__ == "__main__":
    unittest.main()
