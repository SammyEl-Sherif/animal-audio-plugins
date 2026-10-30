# Plugin release pipeline

macOS only during the beta (notarized `.pkg`). The Windows scripts (`release_win_plugin.ps1`, `package_win_plugin.ps1`)
are kept for later but not run by CI.

## Version (independent per plugin, semantic)

| Source | Role |
| --- | --- |
| `plugins/<slug>/VERSION` | **Semver for that product only**; CI commits the bump on release |
| Git tag `<slug>-vX.Y.Z` | Marks what shipped; the base for the next bump |
| Plugin UI build stamp | Same version as that plugin’s VERSION file |
| Artifacts | `dist/releases/<slug>/<version>/…` |
| animal.audio | `plugin_releases` row per slug + platform (upload updates checksums) |

The next version comes from [conventional commits](https://www.conventionalcommits.org/) since the plugin's last tag
that touch `plugins/<slug>/`, `shared/`, `cmake/` or the root `CMakeLists.txt`
([`scripts/plan_release.py`](../scripts/plan_release.py)):

| Commit | Bump |
| --- | --- |
| `feat!: …`, `fix(duck)!: …` or a `BREAKING CHANGE:` footer | major |
| `feat: …` / `feat(duck): …` | minor |
| `fix:` / `bugfix:` / `perf:` | patch |
| `chore:`, `docs:`, `refactor:`, `test:`, `ci:`, anything else | none |

- A plugin with no tag yet releases its `VERSION` as is (first release).
- Bumping `VERSION` by hand in a PR above the last tag releases exactly that version.
- Changes in `shared/` / `cmake/` release every plugin (they ship in every binary); use `refactor:` / `chore:` when
  that is not wanted.

Preview locally: `python3 scripts/plan_release.py` (every PR also shows it in the **release plan** job summary).

Root `CMakeLists.txt` `project(AnimalAudio VERSION 0.0.0)` is the monorepo marker only, not a product version.

---

## Workflows

| Workflow | When | What |
| --- | --- | --- |
| `ci.yml` (**CI**) | every PR | `build.yml` + release-plan preview; `ci-ok` is the one required check |
| `build.yml` (**Build**, reusable) | called by CI / Release | lint (clang-format, repo checks) ∥ one macOS job per plugin: build, ctest, pluginval 10, install smoke test; with `release: true` also sign, notarize, `.pkg`, validate |
| `release.yml` (**Release**) | push to `main`, or manual | plan → build (release) → commit VERSION bumps + tags → per plugin: Supabase upload + GitHub Release |

Release on merge is automatic. Nothing is tagged or uploaded unless every plugin build in that run passed; re-run
the failed jobs and the rest of the run continues. The tag commit is pushed by the bypass GitHub App
(`BYPASS_APP_ID` / `BYPASS_APP_PRIVATE_KEY`), the only non-admin actor allowed past the `main` ruleset.

**Manual release:** Actions → **Release** → Run workflow, pick the plugin and a bump:

```bash
gh workflow run release.yml -f plugin=duck                    # auto: from commits since the last tag
gh workflow run release.yml -f plugin=duck -f bump=patch      # patch | minor | major
gh workflow run release.yml -f plugin=duck -f bump=current    # re-release the current version (re-upload)
gh workflow run release.yml -f plugin=duck -f dry_run=true    # build + sign + notarize only (any branch)
```

Local (Mac with the signing setup): `source ~/.appstoreconnect/notary.env && ./scripts/release_plugin.sh <slug> --upload`.

---

## What ships

| Platform | Artifact | Notes |
| --- | --- | --- |
| Mac | `<Product>-<ver>-mac-installer.pkg` | Bundle signed + notarized + stapled, then the `.pkg` is productsigned (Developer ID Installer) + notarized + stapled. Package id `com.animalaudio.<slug>.vst3`. |

Installer text comes from templates in `scripts/installers/mac/`, filled with the plugin's `PRODUCT_NAME` (from
`plugins/<slug>/CMakeLists.txt`) split into words (`HoneyBadger` → "Honey Badger"). Preview a plugin's Mac installer
resources without signing: `scripts/build_mac_pkg.sh <slug> <path/to/Product.vst3> <version> /tmp/pkg-preview --render-only`.

---

## Releasing a new plugin (first time)

Agents: the **`/release <slug>`** skill (`.cursor/skills/release/SKILL.md`) runs these steps and checks each result.

1. Merge the plugin to `main` with `plugins/<slug>/VERSION` (set the public version in the PR) and green CI.
2. Automatic: the **Release** run for that merge ships it (no tag yet → first release). Check it with
   `gh run list --workflow release.yml --limit 3`.
3. List it on the site: PR in **animal-audio-web** adding the plugin to `packages/catalog/src/plugins.ts`
   (`slug` = `<slug>`, `bundleName` = `<PRODUCT_NAME>.vst3`) and `src/modules/LandingPage/data/plugins.ts`, plus an
   accent theme if it needs a new colour. The new plugin PR's "Site listing" block has the copy. Merging deploys the site.
4. Smoke test: `/plugins/<slug>` shows the Mac download at the new version; the Animal Software Center offers Install.

Updates later: merge `feat:` / `fix:` commits; the merge releases them.

Signing identities and the notary key live only in repo secrets ([docs/GITHUB_ACTIONS_SECRETS.md](GITHUB_ACTIONS_SECRETS.md));
contributors never need Apple credentials.

---

## Scripts

| Script | Role |
| --- | --- |
| [`scripts/lib/plugin_release.py`](../scripts/lib/plugin_release.py) | Catalog: slug, target, product name, paths |
| [`scripts/plan_release.py`](../scripts/plan_release.py) | Which plugins release, at which version (tags + conventional commits) |
| [`scripts/tag_release.py`](../scripts/tag_release.py) | Commit the VERSION bumps, tag `<slug>-vX.Y.Z`, push (never force) |
| [`scripts/release_plugin.sh`](../scripts/release_plugin.sh) | Mac orchestrator (CI: `AA_SKIP_BUILD=1 … --skip-verify` signs the bundle the build job tested) |
| [`scripts/release_mac_plugin.sh`](../scripts/release_mac_plugin.sh) | Build, sign, notarize, staple the bundle |
| [`scripts/build_mac_pkg.sh`](../scripts/build_mac_pkg.sh) | Signed + notarized `.pkg` from the stapled bundle |
| [`scripts/validate_plugin_release.py`](../scripts/validate_plugin_release.py) | Validate `.pkg` / `.zip` |
| [`scripts/upload_plugin_release.py`](../scripts/upload_plugin_release.py) | Supabase Storage upload + publish the active `plugin_releases` row (insert on first release) |

Reusable actions: `.github/actions/setup-mac-build` (Ninja, ccache, cached JUCE/Catch2), `apple-signing`
(keychain + notary settings), `publish-plugin-github-release`.

---

## Validate only

```bash
python3 scripts/validate_plugin_release.py --plugin duck --platform mac \
  --artifact dist/releases/duck/1.0.0/Duck-1.0.0-mac-installer.pkg \
  --require-notarized
```
