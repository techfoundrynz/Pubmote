import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from PIL import Image

spec = importlib.util.spec_from_file_location("visuals", Path(__file__).resolve().parents[1] / "scripts/simulator_visuals.py")
visuals = importlib.util.module_from_spec(spec)
spec.loader.exec_module(visuals)


class VisualReportTest(unittest.TestCase):
    def test_reports_identical_changed_resized_added_and_removed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before, after = root / "base", root / "current"
            before.mkdir(); after.mkdir()
            for folder in (before, after):
                for name in ("same", "changed", "resized"):
                    Image.new("RGBA", (4, 4), "black").save(folder / f"{name}.png")
            image = Image.open(after / "changed.png"); image.putpixel((0, 0), (255, 0, 0, 255)); image.save(after / "changed.png")
            Image.new("RGBA", (5, 4), "black").save(after / "resized.png")
            Image.new("RGBA", (4, 4), "black").save(after / "added.png")
            Image.new("RGBA", (4, 4), "black").save(before / "removed.png")
            summary = visuals.build_report(before, after, root / "report", "<Test>")
            self.assertEqual(summary["counts"], {"added": 1, "changed": 1, "removed": 1, "resized": 1, "identical": 1})
            changed = next(row for row in summary["screens"] if row["name"] == "changed.png")
            self.assertEqual(changed["changed_pixels"], 1)
            self.assertEqual(changed["percent"], 6.25)
            self.assertIn("&lt;Test&gt;", (root / "report/index.html").read_text(encoding="utf-8"))
            self.assertEqual(json.loads((root / "report/summary.json").read_text(encoding="utf-8"))["total"], 5)

    def test_tolerance_and_alpha_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            Image.new("RGBA", (2, 2), (0, 0, 0, 255)).save(root / "base.png")
            Image.new("RGBA", (2, 2), (2, 0, 0, 255)).save(root / "current.png")
            result = visuals.compare_image(root / "base.png", root / "current.png", root / "diff.png", 2)
            self.assertEqual(result["status"], "identical")
            Image.new("RGBA", (2, 2), (0, 0, 0, 0)).save(root / "current.png")
            result = visuals.compare_image(root / "base.png", root / "current.png", root / "diff.png", 2)
            self.assertEqual(result["percent"], 100)


if __name__ == "__main__":
    unittest.main()
