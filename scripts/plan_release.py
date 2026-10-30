#!/usr/bin/env python3
"""Work out which plugins to release and at which semver, from git tags and conventional commits.

    python3 scripts/plan_release.py                         # every plugin with releasable changes
    python3 scripts/plan_release.py --plugin duck --bump minor
    python3 scripts/plan_release.py --github-output         # CI: releases, count, plugins, versions + job summary

Per plugin, the base version is the higher of plugins/<slug>/VERSION and its latest <slug>-vX.Y.Z tag.
- No tag yet: release VERSION as is (first release).
- VERSION above the latest tag: release VERSION as is (a hand-picked version).
- Otherwise bump from the commits since that tag that touch the plugin, shared/ (not shared/testing) or cmake/:
  `type!:` or `BREAKING CHANGE` -> major, `feat` -> minor, `fix` / `bugfix` / `perf` -> patch. Any other type
  (chore, docs, refactor, test, ci, non-conventional) releases nothing.
`--bump patch|minor|major` forces that bump; `--bump current` re-releases the base version.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from lib.plugin_release import get_plugin, list_plugin_slugs  # noqa: E402

BUMPS = ("auto", "patch", "minor", "major", "current")
_LEVEL = {"patch": 1, "minor": 2, "major": 3}
_CONVENTIONAL = re.compile(r"^(?P<type>[a-zA-Z]+)(?:\([^)]*\))?(?P<breaking>!)?:\s")
_TYPE_BUMP = {"feat": "minor", "fix": "patch", "bugfix": "patch", "perf": "patch"}
# Paths (besides plugins/<slug>/) whose changes ship inside every plugin binary; test helpers do not ship.
SHARED_PATHS = ("shared", "cmake", "CMakeLists.txt", ":(exclude)shared/testing")


@dataclass(frozen=True)
class Commit:
    sha: str
    subject: str
    body: str


@dataclass(frozen=True)
class Release:
    slug: str
    product: str
    version: str
    previous: str
    tag: str
    bump: str
    notes: str


def parse_semver(text: str) -> tuple[int, int, int]:
    major, minor, patch = (int(x) for x in text.split("."))
    return major, minor, patch


def bump_version(version: str, bump: str) -> str:
    major, minor, patch = parse_semver(version)
    if bump == "major":
        return f"{major + 1}.0.0"
    if bump == "minor":
        return f"{major}.{minor + 1}.0"
    if bump == "patch":
        return f"{major}.{minor}.{patch + 1}"
    return version


def commit_bump(commit: Commit) -> str | None:
    match = _CONVENTIONAL.match(commit.subject)
    if not match:
        return None
    if match.group("breaking") or re.search(r"^BREAKING[ -]CHANGE:", commit.body, re.MULTILINE):
        return "major"
    return _TYPE_BUMP.get(match.group("type").lower())


def highest_bump(commits: list[Commit]) -> str | None:
    bumps = [b for b in (commit_bump(c) for c in commits) if b]
    return max(bumps, key=_LEVEL.__getitem__) if bumps else None


def git(*args: str, cwd: Path) -> str:
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout


def latest_tag(slug: str, *, cwd: Path) -> tuple[str, str] | None:
    """(tag, version) of the highest <slug>-vX.Y.Z tag, or None."""
    versions = []
    for tag in git("tag", "--list", f"{slug}-v*", cwd=cwd).split():
        version = tag[len(slug) + 2:]
        if re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
            versions.append((parse_semver(version), tag, version))
    if not versions:
        return None
    _, tag, version = max(versions)
    return tag, version


def commits_since(tag: str, slug: str, *, ref: str, cwd: Path) -> list[Commit]:
    out = git("log", "--format=%H%x1f%s%x1f%b%x1e", f"{tag}..{ref}", "--", f"plugins/{slug}", *SHARED_PATHS, cwd=cwd)
    commits = []
    for record in out.split("\x1e"):
        record = record.strip("\n")
        if record:
            sha, subject, body = record.split("\x1f", 2)
            commits.append(Commit(sha, subject, body))
    return commits


def release_notes(commits: list[Commit]) -> str:
    lines = [f"- {c.subject} ({c.sha[:7]})" for c in commits if commit_bump(c)]
    return "\n".join(lines)


def plan_plugin(slug: str, *, bump: str = "auto", ref: str = "HEAD", cwd: Path = ROOT) -> Release | None:
    info = get_plugin(slug, repo_root=cwd)
    file_version = info.version
    tagged = latest_tag(slug, cwd=cwd)
    tag_version = tagged[1] if tagged else None
    base = max(file_version, tag_version or file_version, key=parse_semver)
    commits = commits_since(tagged[0], slug, ref=ref, cwd=cwd) if tagged else []

    if bump == "current":
        version, reason = base, "current"
    elif bump in _LEVEL:
        version, reason = bump_version(base, bump), bump
    elif tag_version is None:
        version, reason = file_version, "first release"
    elif parse_semver(file_version) > parse_semver(tag_version):
        version, reason = file_version, "VERSION"
    else:
        auto = highest_bump(commits)
        if auto is None:
            return None
        version, reason = bump_version(base, auto), auto

    return Release(
        slug=slug,
        product=info.product_name,
        version=version,
        previous=tag_version or "",
        tag=f"{slug}-v{version}",
        bump=reason,
        notes=release_notes(commits),
    )


def plan(slugs: list[str], *, bump: str = "auto", ref: str = "HEAD", cwd: Path = ROOT) -> list[Release]:
    return [r for r in (plan_plugin(s, bump=bump, ref=ref, cwd=cwd) for s in slugs) if r]


def summary(releases: list[Release]) -> str:
    if not releases:
        return "### Release plan\n\nNothing to release (no `feat` / `fix` / `bugfix` / `perf` / breaking commits since the last tags).\n"
    rows = "\n".join(
        f"| {r.product} | {r.previous or '-'} | **{r.version}** | {r.bump} |" for r in releases
    )
    return f"### Release plan\n\n| Plugin | From | To | Why |\n| --- | --- | --- | --- |\n{rows}\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--plugin", help="Only this plugin slug (default: all)")
    parser.add_argument("--bump", choices=BUMPS, default="auto")
    parser.add_argument("--ref", default="HEAD")
    parser.add_argument("--github-output", action="store_true", help="Write releases/count to $GITHUB_OUTPUT")
    args = parser.parse_args(argv)

    slugs = list_plugin_slugs()
    if args.plugin:
        if args.plugin not in slugs:
            raise SystemExit(f"unknown plugin {args.plugin!r}; available: {', '.join(slugs)}")
        slugs = [args.plugin]
    if args.bump != "auto" and not args.plugin:
        raise SystemExit("--bump other than auto needs --plugin")

    releases = plan(slugs, bump=args.bump, ref=args.ref)
    payload = json.dumps([asdict(r) for r in releases])
    print(summary(releases))
    if args.github_output:
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as out:
            out.write(f"releases={payload}\ncount={len(releases)}\n")
            out.write(f"plugins={json.dumps([r.slug for r in releases])}\n")
            out.write(f"versions={json.dumps({r.slug: r.version for r in releases})}\n")
        if os.environ.get("GITHUB_STEP_SUMMARY"):
            with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as out:
                out.write(summary(releases))
    else:
        print(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
