#!/usr/bin/env python3
"""Scaffold a new Animal Audio plugin from templates/plugin.

    python3 scripts/new_plugin.py Wobble --description "Tempo-synced stereo wobbler"

Creates plugins/wobble/ (CMake target `Wobble`, VST3 only): a green scaffold (GainShaperProcessor +
ShaperEditor + Theme—the curve-shaper stack Duck uses). Replace editor/DSP in /vst when the concept needs
a different layout; keep Theme, Motif, and BrandBadge. Picks a hue far from other plugins' `kThemeHue`, a
unique 4-character plugin code, adds a README table row, and adds the slug to the Release plugin
workflow dropdown. Re-run cmake to build.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

README_START = "<!-- PLUGINS:START -->"
README_END = "<!-- PLUGINS:END -->"
# Anchor in .github/workflows/release.yml. The choice values are folder slugs.
_PLUGIN_SLUG_OPTIONS = re.compile(
    r"^        options: # plugin-slugs\n(?:          - [a-z0-9]+\n)+",
    re.MULTILINE,
)


def fail(message: str) -> "None":
    print(f"error: {message}", file=sys.stderr)
    sys.exit(1)


def existing_codes(plugins_dir: Path) -> set[str]:
    codes = set()
    for cmake in plugins_dir.glob("*/CMakeLists.txt"):
        match = re.search(r"PLUGIN_CODE\s+(\w{4})\b", cmake.read_text(encoding="utf-8"))
        if match:
            codes.add(match.group(1))
    return codes


def derive_code(name: str, taken: set[str]) -> str:
    base = re.sub(r"[^A-Za-z0-9]", "", name)
    candidates = [base[:4].ljust(4, "x").capitalize()]
    candidates.append((base[:3] + "x").capitalize())
    candidates += [base[:3].capitalize() + c for c in "abcdefghijklmnopqrstuvwxyz"]
    for candidate in candidates:
        if len(candidate) == 4 and any(c.isupper() for c in candidate) and candidate not in taken:
            return candidate
    fail("could not derive a unique plugin code; pass --code")


def existing_hues(plugins_dir: Path) -> list[float]:
    hues = []
    for header in plugins_dir.glob("*/Source/ui/*Theme.h"):
        match = re.search(r"kThemeHue\s*=\s*(-?[0-9.]+)f?", header.read_text(encoding="utf-8"))
        if match:
            hues.append(float(match.group(1)) % 360.0)
    return hues


def hue_distance(a: float, b: float) -> float:
    d = abs(a - b) % 360.0
    return min(d, 360.0 - d)


def pick_hue(taken: list[float]) -> float:
    """The multiple of 5 degrees that is farthest from every hue already in use."""
    if not taken:
        return 210.0
    return max((float(h) for h in range(0, 360, 5)), key=lambda h: (min(hue_distance(h, t) for t in taken), -h))


def render(text: str, values: dict[str, str]) -> str:
    for key, value in values.items():
        text = text.replace(key, value)
    return text


def plugin_slugs_in_release_workflow(text: str) -> list[str]:
    """Folder slugs in the Release workflow's plugin dropdown."""
    match = _PLUGIN_SLUG_OPTIONS.search(text)
    if not match:
        raise ValueError("release workflow is missing the plugin-slugs choice list")
    return re.findall(r"^          - ([a-z0-9]+)$", match.group(0), re.MULTILINE)


def add_release_workflow_plugin(workflow: Path, slug: str) -> bool:
    """Insert slug into the Release workflow dropdown, keeping the list sorted.

    Returns False when the workflow file is absent or the slug is already listed.
    """
    if not re.fullmatch(r"[a-z0-9]+", slug):
        fail(f"plugin slug {slug!r} must be lowercase letters and digits")
    if not workflow.is_file():
        return False
    text = workflow.read_text(encoding="utf-8")
    slugs = plugin_slugs_in_release_workflow(text)
    if slug in slugs:
        return False
    block = "        options: # plugin-slugs\n" + "".join(
        f"          - {name}\n" for name in sorted([*slugs, slug])
    )
    updated, count = _PLUGIN_SLUG_OPTIONS.subn(block, text, count=1)
    if count != 1:
        fail(f"could not update the plugin dropdown in {workflow}")
    workflow.write_text(updated, encoding="utf-8")
    return True


def add_readme_row(readme: Path, name: str, folder: str, description: str) -> bool:
    if not readme.exists():
        return False
    text = readme.read_text(encoding="utf-8")
    if README_END not in text:
        return False
    row = f"| **{name}** | {description} | `plugins/{folder}` | Scaffolded |\n"
    text = text.replace(README_END, row + README_END)
    readme.write_text(text, encoding="utf-8")
    return True


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("name", help="PascalCase plugin name, e.g. Wobble")
    parser.add_argument("--description", default="", help="One-line description")
    parser.add_argument("--code", help="Unique 4-character plugin code with >= 1 uppercase letter (default: derived)")
    parser.add_argument("--hue", type=float, help="Theme hue in degrees 0-360 (default: farthest from the other plugins)")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent, help="Repo root")
    parser.add_argument("--no-readme", action="store_true", help="Don't add a row to README.md")
    args = parser.parse_args()

    name = args.name
    if not re.fullmatch(r"[A-Z][A-Za-z0-9]*", name):
        fail("name must be PascalCase letters/digits, starting with an uppercase letter (e.g. Wobble)")

    root: Path = args.root
    template = root / "templates" / "plugin"
    plugins_dir = root / "plugins"
    folder = name.lower()
    destination = plugins_dir / folder
    if not template.is_dir():
        fail(f"template not found: {template}")
    if destination.exists():
        fail(f"{destination} already exists")

    taken = existing_codes(plugins_dir)
    code = args.code or derive_code(name, taken)
    if not re.fullmatch(r"[A-Za-z0-9]{4}", code) or not any(c.isupper() for c in code):
        fail("--code must be exactly 4 alphanumeric characters with at least one uppercase letter")
    if code in taken:
        fail(f"plugin code {code!r} is already used by another plugin")

    taken_hues = existing_hues(plugins_dir)
    hue = args.hue if args.hue is not None else pick_hue(taken_hues)
    if not 0.0 <= hue < 360.0:
        fail("--hue must be in [0, 360)")
    close = [t for t in taken_hues if hue_distance(hue, t) < 30.0]
    if close:
        print(f"warning: hue {hue:g} is within 30 degrees of an existing plugin's hue ({close[0]:g}); "
              "pick a more distinct colour scheme", file=sys.stderr)

    description = args.description or f"{name} by Animal Audio"
    values = {
        "__NAME_UPPER__": name.upper(),
        "__NAME_LOWER__": folder,
        "__NAME__": name,
        "__CODE__": code,
        "__HUE__": f"{hue:.1f}",
        "__DESCRIPTION__": description,
    }

    for source in sorted(template.rglob("*")):
        if source.is_dir():
            continue
        relative = source.relative_to(template)
        target = destination / Path(render(str(relative), values))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(render(source.read_text(encoding="utf-8"), values), encoding="utf-8")

    readme_updated = False if args.no_readme else add_readme_row(root / "README.md", name, folder, description)
    workflow_updated = add_release_workflow_plugin(root / ".github" / "workflows" / "release.yml", folder)

    print(f"Created {destination.relative_to(root)} (target {name}, plugin code {code}, theme hue {hue:g})")
    if readme_updated:
        print("Added a row to the README plugin table")
    if workflow_updated:
        print("Added the plugin to the Release workflow dropdown")
    print("\nNext steps:")
    print(f"  cmake -S . -B build                  # re-run so the new folder is picked up")
    print(f"  cmake --build build --target {name}_VST3")
    print(f"  ctest --test-dir build -R '^{folder}\\.'")
    print(f"  scripts/verify_plugin.sh {name}      # build + ctest + pluginval + screenshot")
    print("  then: see .cursor/skills/vst/SKILL.md (DSP, animal motif, palette tuning, presets, docs, release readiness)")
    print(f"  after the PR is merged, a maintainer ships it with /release {folder} (no release-script changes needed)")


if __name__ == "__main__":
    main()
