"""The Release plugin workflow dropdown stays in sync with plugins/."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import new_plugin  # noqa: E402
from lib import plugin_release  # noqa: E402

WORKFLOW = ROOT / ".github" / "workflows" / "release-plugin.yml"


class ReleaseWorkflowPluginChoicesTests(unittest.TestCase):
    def test_dropdown_lists_every_plugin_folder(self) -> None:
        text = WORKFLOW.read_text(encoding="utf-8")
        self.assertEqual(new_plugin.plugin_slugs_in_release_workflow(text), plugin_release.list_plugin_slugs())

    def test_add_inserts_a_slug_in_sorted_order(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            workflow = Path(tmp) / "release-plugin.yml"
            workflow.write_text(WORKFLOW.read_text(encoding="utf-8"), encoding="utf-8")
            self.assertTrue(new_plugin.add_release_workflow_plugin(workflow, "parrot"))
            slugs = new_plugin.plugin_slugs_in_release_workflow(workflow.read_text(encoding="utf-8"))
            self.assertEqual(slugs, sorted([*plugin_release.list_plugin_slugs(), "parrot"]))
            self.assertFalse(new_plugin.add_release_workflow_plugin(workflow, "parrot"))
            self.assertIn("          - mac\n", workflow.read_text(encoding="utf-8"))

    def test_missing_workflow_is_skipped(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            missing = Path(tmp) / "release-plugin.yml"
            self.assertFalse(new_plugin.add_release_workflow_plugin(missing, "duck"))


if __name__ == "__main__":
    unittest.main()
