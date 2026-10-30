#!/usr/bin/env python3
"""Record a release plan in git: commit the new plugins/<slug>/VERSION files, tag them and push.

    python3 scripts/tag_release.py --releases '<json from plan_release.py>' [--remote origin] [--branch main] [--dry-run]

The tag points at the commit that was built (HEAD) plus the VERSION bumps, so the tag's tree is exactly what shipped.
If the branch moved on while the release ran, the same VERSION bumps are committed on top of its tip instead. Existing
tags are left alone (re-releasing a version never moves its tag). Never force-pushes.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from plan_release import parse_semver  # noqa: E402

BOT = ("github-actions[bot]", "41898282+github-actions[bot]@users.noreply.github.com")


def git(*args: str, cwd: Path, check: bool = True) -> subprocess.CompletedProcess[str]:
    print("$ git " + " ".join(args))
    return subprocess.run(["git", *args], cwd=cwd, check=check, capture_output=True, text=True)


def write_versions(releases: list[dict], cwd: Path, *, only_upgrades: bool = False) -> list[dict]:
    """Write each release's VERSION file; returns the releases whose file changed."""
    changed = []
    for r in releases:
        path = cwd / "plugins" / r["slug"] / "VERSION"
        current = path.read_text(encoding="utf-8").strip()
        if current == r["version"]:
            continue
        if only_upgrades and parse_semver(current) > parse_semver(r["version"]):
            continue
        path.write_text(r["version"] + "\n", encoding="utf-8")
        changed.append(r)
    return changed


def commit(releases: list[dict], cwd: Path) -> None:
    paths = [f"plugins/{r['slug']}/VERSION" for r in releases]
    names = ", ".join(f"{r['slug']} {r['version']}" for r in releases)
    git("add", *paths, cwd=cwd)
    git("-c", f"user.name={BOT[0]}", "-c", f"user.email={BOT[1]}", "commit", "-m", f"chore(release): {names} [skip ci]",
        cwd=cwd)


def remote_has_tag(remote: str, tag: str, cwd: Path) -> bool:
    return bool(git("ls-remote", "--tags", remote, f"refs/tags/{tag}", cwd=cwd).stdout.strip())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--releases", required=True, help="JSON list from plan_release.py")
    parser.add_argument("--remote", default="origin")
    parser.add_argument("--branch", default="main")
    parser.add_argument("--dry-run", action="store_true", help="Commit and tag locally, push nothing")
    args = parser.parse_args(argv)
    releases = json.loads(args.releases)
    if not releases:
        print("nothing to tag")
        return 0

    built = git("rev-parse", "HEAD", cwd=ROOT).stdout.strip()
    if write_versions(releases, ROOT):
        commit(releases, ROOT)
    release_commit = git("rev-parse", "HEAD", cwd=ROOT).stdout.strip()

    tags = [r["tag"] for r in releases if not remote_has_tag(args.remote, r["tag"], ROOT)]
    for r in releases:
        if r["tag"] not in tags:
            print(f"{r['tag']} already exists; leaving it where it is")
    for tag in tags:
        git("tag", tag, release_commit, cwd=ROOT)

    git("fetch", args.remote, args.branch, cwd=ROOT)
    tip = git("rev-parse", "FETCH_HEAD", cwd=ROOT).stdout.strip()
    refspecs = [f"refs/tags/{t}" for t in tags]
    if tip == built:
        if release_commit != built:
            refspecs.insert(0, f"{release_commit}:refs/heads/{args.branch}")
    else:
        # The branch moved while we built: put the same bumps on its tip (never rewrite it).
        with tempfile.TemporaryDirectory() as tmp:
            worktree = Path(tmp) / "tip"
            git("worktree", "add", "--detach", str(worktree), tip, cwd=ROOT)
            changed = write_versions(releases, worktree, only_upgrades=True)
            if changed:
                commit(changed, worktree)
                refspecs.insert(0, f"{git('rev-parse', 'HEAD', cwd=worktree).stdout.strip()}:refs/heads/{args.branch}")
            git("worktree", "remove", "--force", str(worktree), cwd=ROOT)

    if not refspecs:
        print("nothing to push")
        return 0
    if args.dry_run:
        print("dry run; would push: " + " ".join(refspecs))
        return 0
    result = git("push", "--atomic", args.remote, *refspecs, cwd=ROOT, check=False)
    print(result.stdout + result.stderr)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
