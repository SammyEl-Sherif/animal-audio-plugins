---
name: release
description: Release one Animal Audio plugin end to end - preflight on main, run the Release plugin workflow (Mac notarized .pkg + Windows zip, Supabase upload, GitHub Release), verify, and list it on animal.audio (animal-audio-web catalog PR). Use for /release <slug>, "ship <plugin>", "publish <plugin> to the site", or after a new plugin PR is merged. Maintainers only (needs the repo secrets and animal-audio-web access).
argument-hint: '<plugin slug, e.g. "duck"> [version bump: patch|minor|major|x.y.z]'
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
git fetch origin && git checkout main && git pull --ff-only
python3 -c "import sys; sys.path.insert(0,'scripts'); from lib.plugin_release import get_plugin as g; p=g('<slug>'); print(p.slug, p.version, p.product_name, p.display_name, p.bundle_name, p.github_tag)"
gh run list --workflow build.yml --branch main --limit 1     # latest main build must be green
gh release view <slug>-v<version> >/dev/null 2>&1 && echo "TAG EXISTS"   # exists if auto-release shipped it: go to step 2
```

- The plugin is merged on `main` (`plugins/<slug>/` with `VERSION`, `CMakeLists.txt` `PRODUCT_NAME` PascalCase, no spaces).
- **Version:** `plugins/<slug>/VERSION` is the release version. If the tag already exists, or the user asked for a bump,
  open a small PR that changes only that file (`patch` for fixes, `minor` for features, `major` for breaking state
  changes; a first public release is usually `1.0.0`), merge it, then continue. Never reuse a published version.
- Saved-state contract: if this is an update, confirm with `git log <previous tag>..main -- plugins/<slug>` that no
  parameter ID, plugin code, or ValueTree identifier was renamed.

## 1. Run the release workflow (Mac, then Windows)

Usually already done: when Build goes green on `main`, `auto-release.yml` runs both platforms for every plugin whose
`<slug>-v<version>` tag does not exist yet. Check `gh run list --workflow auto-release.yml --limit 3` first; if it
succeeded for this version skip to step 2, if one platform failed re-run just that one with the command below.

```bash
gh workflow run release-plugin.yml -f plugin=<slug> -f platform=mac -f upload=true -f publish_github_release=true
gh run watch "$(gh run list --workflow release-plugin.yml --limit 1 --json databaseId -q '.[0].databaseId')" --exit-status
gh workflow run release-plugin.yml -f plugin=<slug> -f platform=win -f upload=true -f publish_github_release=true
gh run watch "$(gh run list --workflow release-plugin.yml --limit 1 --json databaseId -q '.[0].databaseId')" --exit-status
```

Wait a few seconds after `gh workflow run` before `gh run list`, and check the listed run is the one you just
started (plugin + platform in `gh run view`). The two platforms can also run in parallel.

Each run builds Release from `main`, runs the tests, signs + notarizes + staples (Mac: bundle and `.pkg`), validates
the artifact, uploads it to Supabase Storage `plugin-releases/<slug>/<version>/…`, publishes the active
`plugin_releases` row (inserted automatically on a plugin's first release), and attaches the artifact to the GitHub
Release `<slug>-v<version>`.

On failure: `gh run view <id> --log-failed`. Typical causes: notarization rejected (read the notarytool log id in the
output), missing secret (docs/GITHUB_ACTIONS_SECRETS.md), test failure on the runner (fix on `main` first). Re-running
after a partial success is safe: upload replaces the Storage object and updates the same active row.

## 2. Verify what shipped

```bash
gh release view <slug>-v<version>          # .pkg + manifest-mac.json, .zip + manifest-win.json attached
gh run download <mac-run-id> -n <slug>-<version>-mac -D /tmp/<slug>-mac && \
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

1. Signed in on `https://animal.audio/plugins/<slug>`: Mac and Windows download buttons show `<version>`.
2. Animal Software Center lists the plugin and offers Install (or Update for an older stamp).
3. Report: version, tag link, workflow run links, artifact sha256 (from the manifests), web PR link, and what was
   verified by hand versus only by CI.

## Keeping this skill honest

Canonical file: `.cursor/skills/release/SKILL.md`; run `scripts/sync_skills.sh` after editing. Behaviour belongs in
the scripts (`scripts/release_plugin.sh`, `build_mac_pkg.sh`, `upload_plugin_release.py`, the workflow) and their tests,
not in this prose. Keep docs/RELEASE.md in sync.
