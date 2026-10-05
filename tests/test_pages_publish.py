import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("publisher", Path(__file__).resolve().parents[1] / "scripts/publish_pages.py")
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


class PagesPublishTest(unittest.TestCase):
    def test_root_deploy_preserves_previews_and_domain(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); site = root / "site"; source = root / "source"
            (site / "simulator/pr-12").mkdir(parents=True); source.mkdir()
            (site / "simulator/pr-12/index.html").write_text("preview")
            (site / "old.js").write_text("stale")
            (site / "CNAME").write_text("pubmote.com")
            (source / "index.html").write_text("tool")
            publisher.assemble(site, source)
            self.assertEqual((site / "simulator/pr-12/index.html").read_text(), "preview")
            self.assertEqual((site / "CNAME").read_text(), "pubmote.com")
            self.assertFalse((site / "old.js").exists())

    def test_preview_replacement_cleanup_and_immutable_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); site = root / "site"; source = root / "source"
            site.mkdir(); source.mkdir()
            (source / "index.html").write_text("preview")
            (source / "build.json").write_text('{"commit":"a"}')
            publisher.assemble(site, source, "simulator/pr-12")
            publisher.assemble(site, source, "simulator/releases/v1", immutable=True)
            publisher.assemble(site, source, "simulator/releases/v1", immutable=True)
            (source / "build.json").write_text('{"commit":"b"}')
            with self.assertRaises(ValueError):
                publisher.assemble(site, source, "simulator/releases/v1", immutable=True)
            publisher.assemble(site, destination="simulator/pr-12", remove=True)
            self.assertFalse((site / "simulator/pr-12").exists())
            self.assertTrue((site / "simulator/releases/v1").exists())

    def test_rejects_path_escape_and_nonstatic_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            site = Path(directory) / "site"; source = Path(directory) / "source"
            site.mkdir(); source.mkdir()
            for path in ("../escape", "/absolute", ".git/hooks"):
                with self.assertRaises(ValueError):
                    publisher.destination_path(site, path)
            (source / "run.py").write_text("print('must not execute')")
            with self.assertRaises(ValueError):
                publisher.validate_source(source)

    def test_pr_revision_paths_replace_old_builds_and_keep_a_landing_page(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); site = root / "site"; source = root / "source"
            site.mkdir(); source.mkdir()
            (source / "index.html").write_text("preview")
            publisher.assemble(site, source, "simulator/pr-12/" + "a" * 40)
            publisher.assemble(site, source, "simulator/pr-12/" + "b" * 40)
            self.assertFalse((site / "simulator/pr-12" / ("a" * 40)).exists())
            self.assertTrue((site / "simulator/pr-12" / ("b" * 40) / "index.html").exists())
            self.assertIn("b" * 40, (site / "simulator/pr-12/index.html").read_text())


if __name__ == "__main__":
    unittest.main()
