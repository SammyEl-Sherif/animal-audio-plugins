"""Tests for scripts/plan_release.py and scripts/tag_release.py (semver from tags + conventional commits)."""
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import plan_release  # noqa: E402
import tag_release  # noqa: E402
from plan_release import Commit, bump_version, commit_bump, plan_plugin  # noqa: E402


def git(repo: Path, *args: str) -> str:
    return subprocess.run(
        ["git", "-c", "user.name=t", "-c", "user.email=t@t", *args], cwd=repo, check=True, capture_output=True, text=True
    ).stdout.strip()


class CommitBumpTests(unittest.TestCase):
    def test_types(self) -> None:
        cases = {
            "feat: knob": "minor",
            "feat(duck): knob": "minor",
            "fix: click": "patch",
            "bugfix: click": "patch",
            "perf: faster": "patch",
            "feat!: new state": "major",
            "refactor(duck)!: rename": "major",
            "chore: tidy": None,
            "docs: readme": None,
            "Add Parrot": None,
            "Merge pull request #1 from x/y": None,
        }
        for subject, expected in cases.items():
            with self.subTest(subject=subject):
                self.assertEqual(commit_bump(Commit("a" * 40, subject, "")), expected)

    def test_breaking_footer(self) -> None:
        self.assertEqual(commit_bump(Commit("a" * 40, "fix: x", "BREAKING CHANGE: state")), "major")

    def test_bump_version(self) -> None:
        self.assertEqual(bump_version("1.2.3", "patch"), "1.2.4")
        self.assertEqual(bump_version("1.2.3", "minor"), "1.3.0")
        self.assertEqual(bump_version("1.2.3", "major"), "2.0.0")
        self.assertEqual(bump_version("1.2.3", "current"), "1.2.3")


class PlanTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self._tmp.name)
        git(self.repo, "init", "-q", "-b", "main")
        for slug, product in (("duck", "Duck"), ("owl", "Owl")):
            d = self.repo / "plugins" / slug
            d.mkdir(parents=True)
            (d / "CMakeLists.txt").write_text(f'aa_add_plugin({product}\n    PRODUCT_NAME "{product}")\n')
            (d / "VERSION").write_text("1.0.0\n")
        (self.repo / "shared").mkdir()
        (self.repo / "shared" / "a.h").write_text("")
        (self.repo / "docs.md").write_text("")
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-qm", "init")
        git(self.repo, "tag", "duck-v1.0.0")

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def change(self, path: str, message: str) -> None:
        f = self.repo / path
        f.write_text(f.read_text() + "x")
        git(self.repo, "commit", "-qam", message)

    def plan(self, slug: str, bump: str = "auto"):
        return plan_plugin(slug, bump=bump, cwd=self.repo)

    def test_untagged_plugin_releases_its_version_file(self) -> None:
        r = self.plan("owl")
        self.assertEqual((r.version, r.bump, r.previous), ("1.0.0", "first release", ""))

    def test_no_releasable_commits(self) -> None:
        self.change("plugins/duck/CMakeLists.txt", "chore: tidy")
        self.change("docs.md", "feat: docs only")
        self.assertIsNone(self.plan("duck"))

    def test_highest_bump_wins(self) -> None:
        self.change("plugins/duck/CMakeLists.txt", "fix: click")
        self.change("plugins/duck/CMakeLists.txt", "feat: knob")
        r = self.plan("duck")
        self.assertEqual((r.version, r.tag, r.bump, r.previous), ("1.1.0", "duck-v1.1.0", "minor", "1.0.0"))
        self.assertIn("- feat: knob", r.notes)

    def test_shared_changes_release_every_plugin(self) -> None:
        git(self.repo, "tag", "owl-v1.0.0")
        self.change("shared/a.h", "fix: shared")
        self.assertEqual(self.plan("duck").version, "1.0.1")
        self.assertEqual(self.plan("owl").version, "1.0.1")

    def test_shared_test_helpers_do_not_count(self) -> None:
        (self.repo / "shared" / "testing").mkdir()
        (self.repo / "shared" / "testing" / "t.h").write_text("")
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-qm", "fix: test helper")
        self.assertIsNone(self.plan("duck"))

    def test_other_plugin_changes_do_not_count(self) -> None:
        self.change("plugins/owl/CMakeLists.txt", "feat: owl only")
        self.assertIsNone(self.plan("duck"))

    def test_hand_bumped_version_file_wins(self) -> None:
        (self.repo / "plugins/duck/VERSION").write_text("2.0.0\n")
        git(self.repo, "commit", "-qam", "fix: bump")
        r = self.plan("duck")
        self.assertEqual((r.version, r.bump), ("2.0.0", "VERSION"))

    def test_tag_ahead_of_version_file_is_the_base(self) -> None:
        git(self.repo, "tag", "duck-v1.4.0")
        self.change("plugins/duck/CMakeLists.txt", "fix: x")
        self.assertEqual(self.plan("duck").version, "1.4.1")

    def test_forced_bumps(self) -> None:
        self.assertEqual(self.plan("duck", "major").version, "2.0.0")
        self.assertEqual(self.plan("duck", "current").version, "1.0.0")
        self.assertEqual(self.plan("owl", "patch").version, "1.0.1")


class TagReleaseTests(unittest.TestCase):
    def test_write_versions(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            for slug, version in (("duck", "1.0.0"), ("owl", "3.0.0")):
                (repo / "plugins" / slug).mkdir(parents=True)
                (repo / "plugins" / slug / "VERSION").write_text(version + "\n")
            releases = [{"slug": "duck", "version": "1.1.0"}, {"slug": "owl", "version": "2.0.0"}]
            changed = tag_release.write_versions(releases, repo, only_upgrades=True)
            self.assertEqual([r["slug"] for r in changed], ["duck"])
            self.assertEqual((repo / "plugins/duck/VERSION").read_text(), "1.1.0\n")
            self.assertEqual((repo / "plugins/owl/VERSION").read_text(), "3.0.0\n")


class SummaryTests(unittest.TestCase):
    def test_empty_plan(self) -> None:
        self.assertIn("Nothing to release", plan_release.summary([]))


if __name__ == "__main__":
    unittest.main()
