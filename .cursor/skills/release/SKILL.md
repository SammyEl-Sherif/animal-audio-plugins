---
name: release
description: Release one Animal Audio plugin end to end - preflight on main, check or run the Release workflow (Mac notarized .pkg, Supabase upload, GitHub Release; semver from conventional commits), verify, and list it on animal.audio (animal-audio-web catalog PR). Use for /release <slug>, "ship <plugin>", "publish <plugin> to the site", or after a new plugin PR is merged. Maintainers only (needs the repo secrets and animal-audio-web access).
argument-hint: '<plugin slug, e.g. "duck"> [bump: auto|patch|minor|major|current]'
---

# /release: ship a plugin to animal.audio

Request (Claude Code fills this in; in Cursor it is whatever followed `/release`): `$ARGUMENTS`

The pipeline is plugin-agnostic (docs/RELEASE.md). Your job is to run it in order, check each result, and get the
plugin listed on the site. Never add per-plugin branches or wrapper scripts to make a release work: if a plugin
needs something special, fix the generic scripts/templates and their tests in a PR first.

Who can run this: someone with write access to this repo (the workflow uses the Developer ID certificates, notary
key and Supabase token stored as repo secrets) and to **animal-audio-web**. Contributors never need Apple or
Supabase credentials; if the user lacks access, stop and say who has to run it.

## 0. Preflight (all must hold; fix or stop)

```bash
git fetch origin --tags && git checkout main && git pull --ff-only
python3 -c "import sys; sys.path.insert(0,'scripts'); from lib.plugin_release import get_plugin as g; p=g('<slug>'); print(p.slug, p.version, p.product_name, p.display_name, p.bundle_name, p.github_tag)"
python3 scripts/plan_release.py --plugin <slug>               # what an auto release would ship now
gh run list --workflow release.yml --branch main --limit 3     # did the merge already release it?
```

- The plugin is merged on `main` (`plugins/<slug>/` with `VERSION`, `CMakeLists.txt` `PRODUCT_NAME` PascalCase, no spaces).
- **Version** (docs/RELEASE.md): semver comes from conventional commits since the last `<slug>-vX.Y.Z` tag
  (`feat` minor, `fix`/`bugfix`/`perf` patch, `!`/`BREAKING CHANGE` major); a plugin with no tag ships its `VERSION`.
  CI commits the bump to `VERSION` and tags it. Never reuse a published version.
- Saved-state contract: if this is an update, confirm with `git log <previous tag>..main -- plugins/<slug>` that no
  parameter ID, plugin code, or ValueTree identifier was renamed.

## 1. Release (usually already done by the merge)

Every push to `main` runs **Release** (`release.yml`): plan → per plugin build, ctest, pluginval, sign, notarize,
`.pkg` → VERSION bump commit + tag → Supabase upload + `plugin_releases` row + GitHub Release. macOS only during the
beta. If the merge's run succeeded for this version, skip to step 2. Otherwise run it by hand:

```bash
gh workflow run release.yml -f plugin=<slug>                 # bump: auto (default) | patch | minor | major | current
sleep 5; run=$(gh run list --workflow release.yml --limit 1 --json databaseId -q '.[0].databaseId')
gh run watch "$run" --exit-status
```

Check the listed run is the one you started (`gh run view "$run"`). `auto` with nothing releasable plans nothing;
pick a bump. `current` re-uploads the current version (tag left in place). `-f dry_run=true` builds, signs and
notarizes without publishing (any branch).

On failure: `gh run view <id> --log-failed`. Nothing is tagged or uploaded unless all plugin builds pass; fix and
`gh run rerun <id> --failed`. Typical causes: notarization rejected (read the notarytool log id), missing secret
(docs/GITHUB_ACTIONS_SECRETS.md), test or pluginval failure (fix on `main` first). Re-running publish is safe: upload
replaces the Storage object and updates the same active row.

## 2. Verify what shipped

```bash
gh release view <slug>-v<version>          # .pkg + manifest-mac.json attached
gh run download <run-id> -n release-<slug> -D /tmp/<slug>-mac && \
  python3 scripts/validate_plugin_release.py --plugin <slug> --platform mac \
    --artifact /tmp/<slug>-mac/<Product>-<version>-mac-installer.pkg --require-notarized
```

Optionally install the `.pkg` on a Mac (double-click), rescan in Live, and check the build stamp in the plugin UI
matches `<version> | <sha>`. Say plainly which of these you did.

## 3. List it on animal.audio (first release only)

Downloads work as soon as the row exists, but the site and the Animal Software Center only show plugins that are in
the **animal-audio-web** catalog. Open a PR there (read its `AGENTS.md` first; Next.js conventions differ):

1. `packages/catalog/src/plugins.ts`: add an `installablePlugins` entry.
   - `slug`: exactly the plugin folder slug (`plugins/<slug>`), which is also `plugin_releases.plugin_slug`.
   - `bundleName`: exactly `<PRODUCT_NAME>.vst3` from the preflight output (e.g. `HoneyBadger.vst3`, no spaces).
     The Software Center finds installed copies by this name.
   - `code`: next `AA-00N`; `name`: display name in caps (`HONEY BADGER`); `category`, `blurb`, `longDescription`,
     `highlights`: take them from the plugin PR's "Site listing" block or README section.
2. `src/modules/LandingPage/data/plugins.ts`: add the same slug to `ReleasedPluginSlug` and a `plugins` entry
   (`index` next number, `status: "AVAILABLE NOW"`, `price`). Remove any `catalogPreview` placeholder for it.
3. Accent: reuse an existing `PluginAccent` only if it matches the plugin's palette. Otherwise add one (both
   `PluginAccent` types, a theme in `src/modules/LandingPage/data/pluginTheme.ts`, and
   `apps/software-center/src/library/PluginPreview.tsx`), using the `accent`/`panel` colours from
   `plugins/<slug>/Source/ui/<Name>Theme.cpp` so the site matches the plugin.
4. Optional: product image from `docs/images/<slug>-ui.png` into `public/landing/`.
5. `npm test && npm run lint && npm run build`, PR, merge. Merging to `main` deploys the site
   (its `release.yml`); no schema change or migration is needed for a new plugin.

## 4. Smoke test and report

1. Signed in on `https://animal.audio/plugins/<slug>`: the Mac download shows `<version>`.
2. Animal Software Center lists the plugin and offers Install (or Update for an older stamp).
3. Report: version, tag link, workflow run link, artifact sha256 (from the manifest), web PR link, and what was
   verified by hand versus only by CI.

## Keeping this skill honest

Canonical file: `.cursor/skills/release/SKILL.md`; run `scripts/sync_skills.sh` after editing. Behaviour belongs in
the scripts (`scripts/release_plugin.sh`, `build_mac_pkg.sh`, `upload_plugin_release.py`, the workflow) and their tests,
not in this prose. Keep docs/RELEASE.md in sync.
