# Plugin release pipeline

## Version (independent per plugin)

| Source | Role |
| --- | --- |
| `plugins/<slug>/VERSION` | **Semver for that product only** (e.g. Duck `1.0.0`) |
| Plugin UI build stamp | Same version as that plugin’s VERSION file |
| Artifacts | `dist/releases/<slug>/<version>/…` |
| GitHub Releases | Tag `<slug>-vX.Y.Z` (e.g. `duck-v1.0.0`) |
| animal.audio | `plugin_releases` row per slug + platform (upload updates checksums) |

```bash
python3 scripts/read_plugin_version.py duck
python3 scripts/read_plugin_version.py duck --tag
```

**Ship a patch for one plugin:** edit only `plugins/duck/VERSION` (or the relevant slug), commit, run that plugin’s release (the upload updates its `plugin_releases` rows) — no monorepo-wide bump.

Root `CMakeLists.txt` `project(AnimalAudio VERSION 0.0.0)` is the monorepo marker only, not a product version.

---

## Commands

| Goal | Command |
| --- | --- |
| Mac (notarized .pkg) | `source ~/.appstoreconnect/notary.env && ./scripts/release_plugin.sh <slug> --upload` |
| Windows | `.\scripts\release_win_plugin.ps1 -Plugin <slug> -Upload` |
| GitHub Actions | **Release plugin** — choose the plugin and platform |

Every plugin goes through the same pipeline; nothing in the scripts or workflow is per plugin. The
**Release plugin** dropdown lists each `plugins/<slug>` folder (`new_plugin.py` adds a new one). Wrappers
`./scripts/release_duck.sh` and `release_win_duck.ps1` still work.

---

## What each platform ships

| Platform | Artifact | Notes |
| --- | --- | --- |
| Mac | `<Product>-<ver>-mac-installer.pkg` | Bundle signed + notarized + stapled, then the `.pkg` is productsigned (Developer ID Installer) + notarized + stapled. Package id `com.animalaudio.<slug>.vst3`. |
| Windows | `<Product>-<ver>-win-vst3.zip` | Bundle, `Animal-Audio-<Product>-EULA.txt`, notices, and `Install-<Product>.ps1` rendered from `scripts/installers/win/Install-Plugin.ps1`. |

Installer text comes from templates in `scripts/installers/mac/` and `scripts/installers/win/`, filled with the plugin's
`PRODUCT_NAME` (from `plugins/<slug>/CMakeLists.txt`) split into words (`HoneyBadger` → "Honey Badger"). Preview a
plugin's Mac installer resources without signing:
`scripts/build_mac_pkg.sh <slug> <path/to/Product.vst3> <version> /tmp/pkg-preview --render-only`.

---

## Releasing a new plugin (first time)

Agents: the **`/release <slug>`** skill (`.cursor/skills/release/SKILL.md`) runs these steps and checks each result.

1. The plugin is merged to `main` with `plugins/<slug>/VERSION`, green CI and pluginval (`scripts/verify_plugin.sh <Name>`).
   Set the public version in that file first if needed (own small PR).
2. Automatic: once **Build** is green on `main`, **Auto release** (`auto-release.yml`) runs **Release plugin** for
   `mac` and `win` for every plugin whose `<slug>-vX.Y.Z` tag does not exist yet. Manual run (re-run a failed
   platform, or upload-free dry run): GitHub Actions → **Release plugin** → choose the plugin, platform `mac` / `win`
   (`gh workflow run release-plugin.yml -f plugin=<slug> -f platform=mac`). Each run builds, tests, signs, notarizes
   (mac), validates, uploads to Supabase Storage, publishes the active `plugin_releases` row (inserted on the first
   release, updated afterwards) and publishes the `<slug>-vX.Y.Z` GitHub Release.
3. List it on the site: PR in **animal-audio-web** adding the plugin to `packages/catalog/src/plugins.ts`
   (`slug` = `<slug>`, `bundleName` = `<PRODUCT_NAME>.vst3`) and `src/modules/LandingPage/data/plugins.ts`, plus an
   accent theme if it needs a new colour. The new plugin PR's "Site listing" block has the copy. Merging deploys the site.
4. Smoke test: `/plugins/<slug>` shows both downloads at the new version; the Animal Software Center offers Install.

Updates later: bump `VERSION` in the PR; merging it releases Mac + Windows automatically. A merge without a
`VERSION` bump releases nothing.

Signing identities and the notary key live only in repo secrets ([docs/GITHUB_ACTIONS_SECRETS.md](GITHUB_ACTIONS_SECRETS.md));
contributors never need Apple credentials.

---

## Scripts

| Script | Role |
| --- | --- |
| [`scripts/lib/plugin_release.py`](../scripts/lib/plugin_release.py) | Catalog: slug, target, product name, paths |
| [`scripts/release_plugin.sh`](../scripts/release_plugin.sh) | Mac orchestrator |
| [`scripts/release_mac_plugin.sh`](../scripts/release_mac_plugin.sh) | Build, sign, notarize, staple the bundle |
| [`scripts/build_mac_pkg.sh`](../scripts/build_mac_pkg.sh) | Signed + notarized `.pkg` from the stapled bundle |
| [`scripts/release_win_plugin.ps1`](../scripts/release_win_plugin.ps1) | Windows orchestrator |
| [`scripts/validate_plugin_release.py`](../scripts/validate_plugin_release.py) | Validate `.pkg` / `.zip` |
| [`scripts/upload_plugin_release.py`](../scripts/upload_plugin_release.py) | Supabase Storage upload + publish the active `plugin_releases` row (insert on first release) |

Secrets: [docs/GITHUB_ACTIONS_SECRETS.md](GITHUB_ACTIONS_SECRETS.md).

---

## Validate only

```bash
python3 scripts/validate_plugin_release.py --plugin duck --platform mac \
  --artifact dist/releases/duck/1.0.0/Duck-1.0.0-mac-installer.pkg \
  --require-notarized

python3 scripts/validate_plugin_release.py --plugin duck --platform win \
  --artifact dist/releases/duck/1.0.0/Duck-1.0.0-win-vst3.zip --allow-unsigned
```
