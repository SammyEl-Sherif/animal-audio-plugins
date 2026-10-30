---
name: vst
description: Create a new Animal Audio VST3 plugin end to end in this monorepo (scaffold, DSP, animal-themed design with its own palette and Motif—layout flexible, not Duck-cloned, tests, build all plugins and fix build errors, local build+install on the user's machine, pluginval, release readiness, docs, PR with a site-listing block; releasing is /release). Use when the user runs /vst <name or idea> or asks for a new plugin in the series. Enforces DRY and branding (BrandBadge, Theme, shared libraries).
argument-hint: '<plugin name or idea, e.g. "Otter: a tempo-synced stereo wobbler">'
---

# /vst: build a new Animal Audio plugin

Request from the user (Claude Code fills this in; in Cursor it is whatever followed `/vst`): `$ARGUMENTS`

You are adding one plugin to the Animal Audio monorepo. Read `AGENTS.md` first (layout, commands,
realtime-safety rules, branding). This skill is the workflow; AGENTS.md is the reference.

Non-negotiables:

1. **DRY.** Never copy code from Duck (or any plugin). Reuse what is in `shared/`; when you need something
   that exists only in another plugin, or that you are about to write a second time, **extract it to
   `shared/` first** (see "Reuse and extraction"). Plugins must not include files from other plugins.
2. **Animal-themed design; layout follows the concept.** What ties the series together is **Animal Audio
   branding** (`BrandBadge`), the caps **animal name**, a distinct **`Theme` palette**, and a Path-drawn
   **`Motif`** (mascot, ornaments, knob caps—wherever they fit). **UI layout is not prescribed:** compose
   controls in a custom `<Name>Editor`, or reuse `ui::ShaperEditor` only when a tempo-synced curve shaper
   (like Duck) is the right UX. Reuse shared widgets (`RotaryKnob`, `PillButton`, `Drawing.h`, …) and
   `Palette` roles; do not copy Duck's panel structure unless the product is that kind of effect.
3. **Realtime-safe, click-free, state-compatible.** Follow the audio-thread rules in AGENTS.md. Parameter
   IDs and plugin codes are saved-state contract.
4. **Prove it.** Build, run ctest, run pluginval, look at the screenshot. Do not claim DAW validation you
   did not do.
5. **Ship it on their machine.** When you have shell access on the user's computer, **build and install the
   new VST3 locally** before you finish (see §8). They should be able to rescan in Live and load the plugin
   without running cmake themselves.

## 1. Pick and confirm the concept

- Parse `$ARGUMENTS` into: **name** (one animal word, PascalCase, not already in `plugins/`) and **concept**
  (what the effect does to audio, in one sentence). If only an idea is given, choose the animal whose
  behaviour matches the effect (Duck dips, a Cheetah gates fast, a Sloth smooths slowly, a Bat flutters...).
- Interactive session: confirm name + concept in one short question if they are genuinely ambiguous.
  Non-interactive (cloud agent): decide, and write the assumption in the PR description.
- Check what exists: `ls plugins`, `grep -rn kThemeHue plugins/*/Source/ui/`, README plugin table.
  The animal and the hue must both be new.

## 2. Scaffold

```bash
git fetch origin main && git checkout -b feat/<name_lower>-plugin origin/main
python3 scripts/new_plugin.py <Name> --description "<one line>" [--hue <0-360>]
```

First time in the repo? Run the `/setup` prerequisites check (`python3 scripts/install_plugins.py --prereqs`) before
building.

This gives a **starter** that already builds: `GainShaperProcessor` + `ShaperEditor` + `Theme` (Duck's
curve-shaper stack). Treat it as a green baseline—not a layout you must keep. Replace the editor and/or
processor when the concept needs different controls or DSP; keep `Theme`, `Motif`, and `BrandBadge`. The script
picks a hue far from the existing plugins and a unique 4-char plugin code. Run
`scripts/verify_plugin.sh <Name> --no-screenshots` once now so you start from green.

After scaffold, confirm the **whole monorepo still builds** (every plugin plus shared tests):

```bash
scripts/configure.sh    # macOS: optional; enables VST3 copy-after-build for local try-out
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

If anything fails, fix errors before adding DSP or UI (stale CMake cache → remove `build/` and reconfigure).
Never leave the tree red: a new plugin must not break Duck or shared libraries.

## 3. Reuse survey (before writing any DSP/UI code)

Open this table and decide, for every piece the concept needs, whether it already exists.

| Need | Already in shared/ |
| --- | --- |
| smoothing / declick | `dsp/Smoothing.h` `OnePoleSmoother` |
| tempo divisions, host-locked or free-run phase | `dsp/TempoSync.h` `Division`, `PhaseTracker`; `core/TransportReader.h` |
| editable curve model + realtime LUT | `dsp/ShapeCurve.h` `ShapeCurve`, `CurveTable` |
| curve stored in plugin state, published to audio thread | `core/CurveState.h` |
| curve to gain engine with scope data | `dsp/CurveGainShaper.h`, `dsp/CycleScope.h` |
| standard params (division, mix, fallbackBpm), state save/restore, phase/transport plumbing | `core/CurveShaperProcessor.h` (`GainShaperProcessor` for gain effects) |
| 16 stock curve shapes | `dsp/StandardPresets.h` (you supply names) |
| curve-shaper editor (optional) | `ui/ShaperEditor.h` + `ui/Theme.h` — use when the UX matches Duck; otherwise a custom editor |
| branding + look | `BrandBadge`, `Theme` (`Palette` + `Motif`), `makePaletteFromHue`, `makeFont` (with kerning) |
| widgets (mix and match) | `PillButton`, `ChoicePillRow`, `RotaryKnob`/`ThemedKnob`, `AccentKnob` (one colour per control), `CurveEditor`, `CurvePresetPanel`, `Drawing.h` |
| panels, title bar, recessed display, piano-roll rows | `ui/Panels.h` |
| value text ("+2 st", cents, dotted status lines) | `core/TextFormat.h` |
| monophonic pitch detection (voice, bass, lead) | `dsp/PitchTracker.h` (YIN, ~2.7 ms hop); YIN pieces in `dsp/Yin.h` |
| offline pitch track of a recording (robust voicing, no octave slips) | `dsp/ProbabilisticYin.h` (pYIN + HMM) |
| splitting a curve into steady segments (notes, levels) | `dsp/ChangePoints.h` (PELT) |
| pitch shifting by grains (TD-PSOLA marks and windows) | `dsp/Psola.h` |
| note names, black keys | `dsp/Notes.h` |
| band-limited fractional reads / resampling, FIR design | `dsp/WindowedSinc.h` `SincReader` (`applyLinear` for plain buffers), `designLowpass` |
| 4x oversampling for nonlinear stages | `dsp/Oversampler.h` `Oversampler4x` |
| tube saturation / drive | `dsp/TubeDrive.h` (two 12AX7 stages, level-matched, clean at 0) |
| test suites | `shared/testing`: `AA_SHAPER_PLUGIN_TESTS`, `AA_GAIN_SHAPER_PLUGIN_TESTS` |
| numeric DSP test helpers (tone amplitude, peak frequency, cents) | `shared/testing`: `SignalProbe.h` |
| synthetic singing for voice tests (notes, vibrato, consonants, legato) | `shared/testing`: `VoiceSynth.h` |

Skim the headers (`ls shared/*/include/animalaudio/*`) rather than trusting this table; it can be stale.
If your effect needs something the base class does not offer (another parameter, a different DSP,
a second visual), prefer extending the shared class with a hook or a template over forking it.

## 4. Theme: palette + animal (series identity)

Everything plugin-specific about **look and personality** is in `plugins/<name>/Source/ui/<Name>Theme.{h,cpp}`,
the Motif, and your editor layout (custom or `ShaperEditor`).

**Palette convention** (`animalaudio::ui::Palette`, roles are fixed, values are yours):

| Role | Use | Duck example |
| --- | --- | --- |
| `background`, `panel`, `panelRaised`, `outline` | dark, low-contrast hue-tinted surfaces | warm charcoal |
| `text`, `textDim` | labels, read-outs | cream |
| `accent` | primary highlight: knob arc, selected pill, curve, title | duck yellow |
| `accentAlt` | gradient partner of the accent | bill orange |
| `onAccent` | text on top of `accent` | near-black |
| `detail` | inactive bend handles, ornaments | bill |
| `ambient` | animated background motif (ripples), kept faint | pond blue |

Rules:

- `kThemeHue` in `<Name>Theme.h` is the plugin's signature hue; `new_plugin.py` reads it to keep hues
  at least ~30 degrees apart. Keep it in sync with the palette you end up with.
- Start from `makePaletteFromHue(kThemeHue)`. Hand-tune only what looks off (write the struct out like
  Duck once the design settles). Keep dark surfaces, one saturated accent, and readable contrast
  (`text` on `panel`, `onAccent` on `accent`).
- Colours are **only** in the Theme/Motif files. No colour literals in shared code or elsewhere in the plugin.

**Animal convention** (`ui::Motif` subclass in `Source/ui/<Name>Motif.{h,cpp}`, set `theme.motif`):

- Implement the Motif hooks your UI uses: `drawMascot`, `drawKnobCap`, `drawAmbient`, `drawDecoration`
  (`ShaperEditor` calls all of them; a custom editor uses what fits—hero mascot, animated background, knob
  cap, corner ornaments). Defaults are a paw, plain cap, and ripples.
- Draw with `juce::Path` and colours from the passed `Palette` only. No bitmaps, no external fonts.
- Set `theme.name` (upper-case animal) and `theme.tagline` (what it does, e.g. "VOLUME SHAPER", "STEREO WOBBLER").
- Every editor must show `BrandBadge` somewhere sensible.
- Preset/curve names: animal-flavoured when you keep curve presets; drop or replace preset UI if the effect
  is not curve-based.

**Layout:** size and control placement are yours. Playful but clean (AGENTS.md **Branding**): dark base,
saturated accent, rounded shapes, readable type. When a control pattern appears in two plugins, extract a
shared widget to `shared/ui` instead of cloning layout code.

## 5. DSP

- Scaffold still on `GainShaperProcessor`: tune presets/theme, or replace processor/editor when the concept
  needs different parameters or algorithms.
- Tempo-synced **curve shapers**: `core::CurveShaperProcessor` / `GainShaperProcessor`; optional `ShaperEditor`.
- **Everything else**: `juce::AudioProcessor` or a new shared base in `core/`; JUCE-free DSP in `shared/dsp` or
  `plugins/<name>/Source/dsp`, with numeric unit tests (bounds, click-freeness, block-size independence, bypass).
- Extending `CurveShaperProcessor`: implement `prepareEffect`, `curveTableChanged`, `renderCycle`.
- Extra parameters: add them in your processor with new, versioned `ParameterID`s; never rename the shared ones.
- Audio-thread rules (AGENTS.md): no allocation/locks/I/O, smooth every audible change, denormals, any block
  size, transport may be absent.

## 6. Reuse and extraction (the DRY policy)

Rule of two: **the second use of a piece of code is the moment it moves to `shared/`.** Do the move
first, as its own commit, then use it from both places.

Decision checklist (answer for every non-trivial class/function/constant you write or find in Duck):

1. **Search first.** `rg` shared/ and plugins/ for the idea, not just the name. If it exists, use it.
2. **Is this the second use?** (another plugin already has it, or you are about to write it twice) -> extract now.
3. **Is it first-use but obviously generic?** (math, a widget, plumbing with no animal in it) -> still write it
   in the right shared library with a plugin-agnostic API, because the next plugin will need it.
4. **Is it data or personality?** (colours, names, presets' names, mascot drawing, tagline, a plugin's own DSP
   maths) -> stays in the plugin, behind the Theme/Motif/constructor-argument seam.
5. **Where does it go?** JUCE-free -> `shared/dsp`; JUCE but no GUI -> `shared/core`; GUI -> `shared/ui`;
   test helpers -> `shared/testing`; build logic -> `cmake/`; repetitive file scaffolding -> `templates/plugin`.
6. **How is it parameterised?** Prefer, in order: values from `Theme`/`Palette`, constructor/function arguments,
   a virtual hook (`Motif`, processor hooks), a template parameter. Never `#ifdef PLUGIN_X` and never a copy with edits.
7. **Does it change another plugin?** It must not: after extracting, Duck's tests pass unchanged and its
   headless screenshot looks the same (regenerate and compare `docs/images/duck-ui*.png`). Saved-state
   identifiers (parameter IDs, ValueTree identifiers) stay byte-identical.
8. **Move the tests with the code** (to the shared library's `tests/` or into `shared/testing` suites), and add
   a test for the new seam.
9. **Update the docs in the same PR**: AGENTS.md layout section, and the skill's reuse table if the inventory changed.
10. **Don't over-extract.** Two things that merely look alike but must diverge stay separate. No speculative
    options nobody uses; no abstraction for a single caller unless item 3 applies.

Red flags in review: a file that is mostly a Duck file with names changed; `#include` paths that leave the
plugin folder (other than `animalaudio/...`); colour literals outside Theme/Motif files; the same helper
in two plugins; shared code mentioning a plugin or an animal by name.

`scripts/check_repo.sh` enforces the mechanical parts (no cross-plugin includes, skill mirrors in sync).

## 7. Tests

- While the plugin still uses `GainShaperProcessor` + `ShaperEditor`, keep `AA_SHAPER_PLUGIN_TESTS`,
  `AA_REAL_MOUSE_PLUGIN_TESTS`, and `AA_GAIN_SHAPER_PLUGIN_TESTS` in `tests/<Name>Tests.cpp` (real mouse suite:
  run ctest under `xvfb-run -a` on Linux). After a custom processor/UI, replace or supplement with plugin-specific
  tests (parameters, state round-trip, headless `createComponentSnapshot` of your editor).
- Add tests for everything plugin-specific: your DSP (JUCE-free where possible), extra parameters and
  their state round-trip, anything you changed in the shared libs (tests belong next to the shared code).
- A regression found in one plugin becomes a shared suite test, so every plugin gets it.

## 8. Verify

```bash
scripts/format.sh                                  # CI runs clang-format --dry-run --Werror
PLUGINVAL=/path/to/pluginval scripts/verify_plugin.sh <Name>
```

`verify_plugin.sh` configures, builds `<Name>_VST3` + tests, runs ctest (`dsp`, `core`, `<name>`), writes the
headless screenshots to `docs/images/<name>-ui*.png` and runs pluginval at strictness 10 (download from
https://github.com/Tracktion/pluginval/releases if missing; on Linux it needs `xvfb-run`).
Then **look at** `docs/images/<name>-ui.png`: distinct palette from the other plugins, animal drawn,
nothing clipped, text readable.

**Full tree:** `cmake --build build --parallel` and `ctest --test-dir build --output-on-failure` so every
VST3 target and shared test stays green. Fix any compile/link/test regressions you introduced. Run
`scripts/check_repo.sh`.

### Build and install locally for the user (required when you can)

If this session runs on the **user's machine** (typical Cursor agent with shell access), you must **compile
and install** the new plugin into their OS VST3 folder—not only run tests in `build/`. Do this after §8
passes (or as the last step of verify). Use `scripts/install_plugins.py`; do not reimplement paths by hand.

```bash
# Preferred: clean Release build of just the new plugin + install (folder = lowercase PascalCase name)
python3 scripts/install_plugins.py --build --clean --only <name_lower>

# If build/ is already green from verify and you only need install:
cmake --build build --target <Name>_VST3 --parallel
python3 scripts/install_plugins.py --only <name_lower>
```

`<name_lower>` is the plugin folder under `plugins/` (e.g. `honeybadger` for target `HoneyBadger`). The script
prints each **installed path** and **build stamp**; repeat that list to the user.

**Host steps** (same as `/setup`; say this every time):

1. **Ableton Live → Preferences → Plug-ins** — enable **Use VST3 Plug-In System Folders**.
2. **Rescan** (hold **Option** / **Alt** for a full rescan if the plugin does not appear).
3. Add **Plug-Ins → VST3 → Animal Audio →** the plugin's display name (`getName()` / UI title).
4. Confirm the build stamp (hover **ANIMAL AUDIO** badge) matches what the install script printed.

**Cloud / no local shell:** skip install, say so explicitly, and give the two commands above plus a pointer
to `.cursor/skills/setup/SKILL.md` for a full-series install.

**Bundle name:** In `aa_add_plugin`, set `PRODUCT_NAME` to the PascalCase target name (e.g. `HoneyBadger`) so
the artefact is `<Name>.vst3`. Spaces in `PRODUCT_NAME` break `verify_plugin.sh` and confuse install paths.
Marketing names (e.g. "Honey Badger") belong in `getName()` and `theme.name` only; the release installers derive the
same words from `PRODUCT_NAME` by splitting the PascalCase.

### Release readiness (the plugin must be shippable as merged)

After merge a maintainer releases it with `/release <name_lower>` and no further code changes. Check:

- `plugins/<name_lower>/VERSION` exists and is semver (`0.1.0` from the scaffold; the maintainer picks the public
  version at release time).
- `PRODUCT_NAME` in `CMakeLists.txt` is the PascalCase target (no spaces); `getName()` / `theme.name` are its words
  ("HoneyBadger" → "Honey Badger" / "HONEY BADGER"). Installer text, the Windows install script and the release file
  names are derived from it automatically.
- Plugin code and every parameter ID / ValueTree identifier are final: they are the saved-state contract once released.
- `pluginval` passes at strictness 10 locally (CI runs 5 on Linux) and CI is green on Linux, macOS and Windows.
- `docs/images/<name_lower>-ui.png` is committed (README and the website product page use it).
- No changes under `scripts/release*`, `scripts/build_mac_pkg.sh`, `scripts/installers/` or
  `.github/workflows/release-plugin.yml` for this plugin. If the generic pipeline really cannot ship it, fix the
  generic code with tests in a separate PR; never add `if slug == ...` or a `release_<name>.sh` wrapper.

## 9. Docs

- README: the plugin-table row (the script adds a stub; replace the status with what you really verified),
  a short section like Duck's with the screenshot, and the layout/test count if it changed.
- AGENTS.md: add the plugin to the table, a short "<Name> specifics" section if it has its own DSP or
  parameters, and any new shared piece in the layout section.
- Remove screenshots you generated for variants you do not reference.

## 10. Commit, push, PR

- Commit per logical change: (a) shared extractions, (b) the plugin, (c) docs/screenshots.
- Push and open the PR to `main` (draft is fine). No write access to the repo (`git push` is denied)? Fork instead:
  `gh repo fork --remote --remote-name fork && git push -u fork HEAD && gh pr create --repo <upstream owner>/<repo> --base main`.
- PR description: what the plugin does, animal + palette hue, what was extracted to shared/ (and which plugins it
  touched), exactly what was verified (ctest count, pluginval level, screenshot) and what was not (macOS/Windows,
  real DAW), assumptions you made. End it with a **Site listing** block the maintainer copies into the
  animal-audio-web catalog at release time:

  ```text
  slug: <name_lower>            bundle: <Name>.vst3           display name: <WORDS IN CAPS>
  category: <e.g. Stereo wobbler>
  blurb: <one sentence, playful but clean>
  long description: <two sentences>
  highlights: <three short phrases>
  accent: <hex of palette.accent> on <hex of palette.panel>   (from Source/ui/<Name>Theme.cpp)
  ```
- Releasing is not part of this skill: after merge a maintainer runs `/release <name_lower>` (signs + notarizes on
  Mac with repo secrets, uploads, publishes the GitHub Release, lists it on animal.audio). See docs/RELEASE.md.
- Final report to the user, briefly: PR link (if any), what was built, what was shared/extracted, verification,
  **installed VST3 path(s) and build stamp** (if you ran install), Ableton rescan reminder, caveats (DAW not
  tried unless they confirmed load), and the next step: "once merged, a maintainer runs `/release <name_lower>`".
