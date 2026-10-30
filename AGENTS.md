# AGENTS.md

Guide for AI agents and contributors working in this repo. Keep it accurate: if you change the layout,
commands, or conventions, update this file in the same change (CLAUDE.md and `.cursor/rules/` point here).

## What this is

**Animal Audio** builds a series of Ableton-compatible **VST3 audio effects** "with juice": bold, playful
UIs, tight DSP. One monorepo, one plugin per `plugins/<name>/`, shared code in `shared/`.
Stack: **JUCE 8 + CMake (C++17)**, Catch2 for tests. VST3 only (Standalone optional for debugging).

| Plugin | Folder | CMake target | Plugin code |
| --- | --- | --- | --- |
| Duck (volume ducking / shaper) | `plugins/duck` | `Duck` (`Duck_VST3`), tests: `DuckTests` | `Duck` |
| Turtle Time (half-time / time warp) | `plugins/turtletime` | `TurtleTime` (`TurtleTime_VST3`), tests: `TurtleTimeTests` | `Turt` |

New plugins: use the **`/vst <name or idea>`** skill (see [Creating a plugin with `/vst`](#creating-a-plugin-with-vst)).

### Plugin lifecycle (clone to live on animal.audio)

| Step | Who | How |
| --- | --- | --- |
| 1. Clone, build, install everything | any dev | **`/setup`** (`scripts/install_plugins.py --build --clean`) |
| 2. Make the plugin | contributor | **`/vst <name or idea>`**: scaffold, DSP, Theme + Motif, tests, pluginval, local install, docs, release-readiness check |
| 3. PR to `main` | contributor | branch `feat/<slug>-plugin` (fork if no write access); CI green on Linux/macOS/Windows; PR ends with a "Site listing" block |
| 4. Review + merge | maintainer | DRY/branding/realtime review against this file |
| 5. Release | CI / maintainer | Automatic on merge when `plugins/<slug>/VERSION` has no tag yet (`auto-release.yml`, after Build is green); else **`/release <slug>`**. Release plugin workflow (Mac notarized `.pkg`, Windows zip) → Supabase upload + `plugin_releases` row → GitHub Release `<slug>-vX.Y.Z` |
| 6. List on the site | maintainer | `/release` step 3: catalog PR in **animal-audio-web** (`packages/catalog`, landing data, accent) |

Contributors never need Apple or Supabase credentials and never edit release scripts for their plugin. Skills:
`.cursor/skills/{setup,vst,release}/SKILL.md` (canonical; `.claude/skills/` mirrors via `scripts/sync_skills.sh`).

## Layout

```
CMakeLists.txt        options + FetchContent + auto-discovers plugins/*/CMakeLists.txt
cmake/                FetchDependencies.cmake (JUCE/Catch2 pins), AnimalAudioPlugin.cmake (aa_add_plugin)
shared/dsp/           animalaudio::dsp  INTERFACE, pure C++17, NO JUCE. Tests in shared/dsp/tests
  Smoothing.h         OnePoleSmoother (parameter/gain declick)
  TempoSync.h         Division (1/8..1/1), beatsPerCycle, PhaseTracker (host-locked or free-run phase)
  ShapeCurve.h        node/bend curve (message thread) + CurveTable (baked LUT for the audio thread)
  CurvePreset.h       CurvePreset / CurvePresetList;  StandardPresets.h: the 16 stock shapes (plugin supplies names)
  CurveGainShaper.h   curve -> gain engine (declick, mix, scope feed);  CycleScope.h: audio->UI scope bins
shared/core/          animalaudio::core INTERFACE (sources compile into each plugin): JUCE but no GUI
  CurveState          curve stored in the APVTS ValueTree + lock-free-ish publish to the audio thread
  CurveShaperProcessor  base AudioProcessor: standard params (division, mix, fallbackBpm), state save/restore,
                      transport + phase plumbing; GainShaperProcessor = same with CurveGainShaper DSP
  TransportReader.h   AudioPlayHead -> dsp::TransportInfo
shared/ui/            animalaudio::ui   INTERFACE lib whose .cpp files compile into each plugin
  Palette.h           colour roles every widget draws from; makePaletteFromHue(); makeFont()
  Theme.h             Theme { Palette, name, tagline, Motif }; Motif = the animal (mascot, knob cap, ambient, decoration)
  ShaperEditor        optional shared curve-shaper editor (Duck uses it at 820x470); not required for every plugin
  BrandBadge, PillButton, ChoicePillRow, RotaryKnob/ThemedKnob, CurveEditor, CurvePresetPanel, Drawing.h
shared/testing/       animalaudio::testing header-only Catch2 suites: AA_SHAPER_PLUGIN_TESTS, AA_GAIN_SHAPER_PLUGIN_TESTS
plugins/duck/         only what is Duck-specific:
  Source/             DuckProcessor (GainShaperProcessor + presets), DuckEditor (ShaperEditor + DuckTheme)
  Source/dsp/         DuckPresets (names for the standard shapes)
  Source/ui/          DuckTheme (palette, kThemeHue), DuckMotif (rubber duck, duck-head knob cap, feathers)
  tests/              DuckTests.cpp = the two shared suite macros
plugins/turtletime/  half-time / time warp (own processor, not the curve shaper):
  Source/             TurtleTimeProcessor, TurtleTimeEditor
  Source/dsp/         TurtleTimeEngine (circular-buffer resample, 4-point cubic, keep-pitch grains), TimePatterns
  Source/ui/          TurtleTimeTheme (kThemeHue 147), TurtleTimeMotif (hex-shell mark, machined knob cap),
                      TurtleTimeWidgets (TurtleKnob, PatternCell)
  tests/              TurtleTimeTests.cpp
templates/plugin/     starter copied by scripts/new_plugin.py (placeholders __NAME__, __NAME_LOWER__, __HUE__, ...)
.cursor/skills/       the /setup, /vst and /release skills (canonical);  .claude/skills/ holds copies made by scripts/sync_skills.sh
scripts/              new_plugin.py, install_plugins.py (+ tests/), verify_plugin.sh, release_plugin.sh / release_win_plugin.ps1 (any slug), build_mac_pkg.sh, installers/ (mac + win templates), validate/upload release helpers, format.sh, sync_skills.sh, check_repo.sh (docs/RELEASE.md)
cmake/                FetchDependencies, AnimalAudioPlugin (aa_add_plugin), AnimalAudioBuildInfo + GenerateBuildInfo (build stamp)
docs/images/          README screenshots (generated by tests, see below)
```

## Build, test, run

New contributors: the **`/setup`** skill (`.cursor/skills/setup/SKILL.md`) is the guided clone → build →
Ableton rescan path. New plugins: **`/vst`** (below).

```bash
scripts/configure.sh                                        # macOS: auto-install VST3 after plugin builds
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # fetches JUCE + Catch2 (needs network once)
cmake --build build --parallel                              # all plugins + tests
cmake --build build --target Duck_VST3                      # just the plugin bundle
ctest --test-dir build --output-on-failure                  # all tests (dsp.*, core.*, duck.*, ...)
./build/shared/dsp/animalaudio_dsp_tests                    # fast, JUCE-free
./build/plugins/duck/DuckTests "duck: editor*"             # run a Catch2 filter directly
scripts/verify_plugin.sh Duck                               # build + ctest + screenshots + pluginval for one plugin
```

- Offline / cached deps: `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE -DFETCHCONTENT_SOURCE_DIR_CATCH2=/path/to/Catch2`.
- Options: `AA_BUILD_TESTS` (ON), `AA_BUILD_STANDALONE` (OFF), `AA_COPY_PLUGINS_AFTER_BUILD` (OFF; ON via `scripts/configure.sh` on macOS), `AA_LOCAL_BUILD` (OFF; ON via `configure.sh` / `install_plugins.py --build` so dev bundles are `… Local.vst3`; release scripts set OFF), `AA_PLUGINS="duck;..."`.
- Linux needs the X11/ALSA/freetype dev packages listed in README.md. Most tests do not need a display: UI is
  rendered to an `Image` via `createComponentSnapshot`. The `real mouse` suite (`shared/testing/.../RealMouseSuite.h`,
  `AA_REAL_MOUSE_PLUGIN_TESTS`) puts the editor in a native window and injects events through `ComponentPeer`, at several
  editor/desktop scales; it needs a display, so run ctest as `xvfb-run -a ctest ...` on Linux (it skips without one) and
  keep those tests serial (`RESOURCE_LOCK native_display` in each plugin's `catch_discover_tests`): they move the
  real OS pointer, so two at once on one X server fight over it.
- Screenshots for the README: `AA_SCREENSHOT_DIR=docs/images ./build/plugins/duck/DuckTests "duck: editor renders headlessly"`.
  Regenerate and commit them whenever the Duck UI changes visibly.
- Plugin validation: [pluginval](https://github.com/Tracktion/pluginval) (`xvfb-run -a pluginval --strictness-level 10 --validate-in-process <bundle>.vst3`);
  CI runs level 5 on Linux. Duck passes level 10 (incl. editor, automation, state restore).
- Scaffold check: CI's `scaffold` job runs `new_plugin.py` for a throwaway plugin and builds + tests it, so
  `templates/plugin` can never ship a red starter. Changing the template? Run the same locally in a git worktree.
- Format: `scripts/format.sh` (CI checks `clang-format --dry-run --Werror` on all `*.h`/`*.cpp`).
- Script checks: `scripts/check_repo.sh` runs `bash -n` on `scripts/*.sh`, `py_compile` on the Python scripts, the
  install-script unit tests and, when `pwsh` exists (CI), the PowerShell parser on `scripts/*.ps1`.
- Debug runs of the built plugin in a host are not possible in the cloud VM; do not claim host validation
  you did not do. State plainly what was and was not verified.

## Installing on a machine with `/setup`

Run the **`/setup`** skill (`.cursor/skills/setup/SKILL.md`, mirrored for Claude Code; same sync rule as `/vst`). It runs
`scripts/install_plugins.py`, which does the deterministic work and is also usable by hand:
`--prereqs` / `--install-prereqs`, `--build --clean` (clean Release build of every folder under `plugins/`, tests off),
install into the OS VST3 folder (macOS `~/Library/Audio/Plug-Ins/VST3`, Windows `%COMMONPROGRAMFILES%\VST3` if admin else
`%LOCALAPPDATA%\Programs\Common\VST3`, Linux `~/.vst3`; `--user` / `--system` / `--prefix`), removing stale copies from the
other folders, clearing macOS quarantine + ad-hoc codesign, then printing each installed path with its build stamp.
`--list`, `--uninstall` and `--dry-run` also exist. Plugins are auto-discovered (no list to maintain). Behaviour changes go
in the script and `scripts/tests/test_install_plugins.py` (run by `scripts/check_repo.sh` and CI, which also smoke-tests
the script against a temp prefix on Linux/macOS/Windows after the build). The skill's last step tells the user to enable
*Use VST3 Plug-In System Folders*, Rescan (Alt/Option for a full rescan) and test in Live.

## Build stamp

Each plugin’s **release semver** is `plugins/<slug>/VERSION` (independent per product). `aa_add_plugin` reads that file and
`aa_attach_plugin_build_info` generates `build/generated/build_info/<Target>/animalaudio_build_info.h` for that target only
(see [docs/RELEASE.md](docs/RELEASE.md)). Bump one plugin’s VERSION and run its release pipeline without affecting others.
GitHub release tags are `<slug>-vX.Y.Z` (e.g. `duck-v1.0.0`). Releasing is
plugin-agnostic: `scripts/release_plugin.sh <slug>` / the **Release plugin** workflow (slug input) build, sign, notarize and
package any folder under `plugins/`; installer text and the EULA/install-script names are derived from `PRODUCT_NAME`
(templates in `scripts/installers/`), and the upload publishes the `plugin_releases` row, inserting it on a plugin's first
release (see docs/RELEASE.md). Never add per-plugin branches (`if slug == ...`) or `release_<name>.sh` wrappers to release
scripts; the `*_duck*` scripts are legacy aliases kept for muscle memory, not a pattern to copy.

Every plugin shows `version | git SHA | date` in the UI. Version in the stamp comes from that plugin’s VERSION file; git SHA and
UTC date come from `cmake/GenerateBuildInfo.cmake` (`AA_BUILD_SHA` / `AA_BUILD_DATE` override). The binary embeds
`AA-BUILD|version|sha|date|`, which `install_plugins.py` reads without loading a host.

## Creating a plugin with `/vst`

Run the **`/vst <name or idea>`** skill (`.cursor/skills/vst/SKILL.md`, mirrored for Claude Code in
`.claude/skills/vst/SKILL.md`; edit the Cursor file, then run `scripts/sync_skills.sh`; CI fails if the
mirror is stale). It drives the whole job: pick the animal and concept, scaffold, DSP, theme, tests,
`scripts/verify_plugin.sh` (build + ctest + pluginval + screenshot), **local install via
`install_plugins.py` on the user's machine**, release-readiness check, README/AGENTS.md, PR (with a "Site listing"
block). Releasing it afterwards is the maintainer's **`/release <slug>`** skill (`.cursor/skills/release/SKILL.md`).

By hand, the scaffold is:

```bash
python3 scripts/new_plugin.py Wobble --description "One-line description" [--hue 200]   # PascalCase name
cmake -S . -B build && scripts/verify_plugin.sh Wobble
```

The script copies `templates/plugin` to `plugins/wobble`, substitutes the placeholders, picks a unique
4-char plugin code (manufacturer code is always `AnAu`) and a theme hue far from every other plugin's
`kThemeHue`, adds a row to the README plugin table, and adds the slug to the Release plugin
workflow dropdown. The result is a **green scaffold**: `GainShaperProcessor` + `ShaperEditor` + `Theme` (Duck's curve-shaper stack)
and the standard shaper test suites. New plugins are not required to keep that layout—swap editor and/or
processor when the concept needs it; keep Animal Audio branding (`BrandBadge`, palette, Motif).

### Theme: every plugin gets its own palette and animal

- Palette roles are fixed (`ui::Palette`: background/panel/panelRaised/outline, text/textDim,
  accent/accentAlt/onAccent, detail, ambient); values are per plugin in `plugins/<name>/Source/ui/<Name>Theme.cpp`,
  started from `makePaletteFromHue(kThemeHue)` and hand-tuned. Hues of different plugins stay >= ~30 degrees apart.
- The animal is a `ui::Motif` subclass (mascot, knob cap, ambient background, decoration) using palette colours only.
- Colour literals live only in Theme/Motif files. **UI layout is per plugin** (custom editor or shared widgets);
  `ui::ShaperEditor` is the optional curve-shaper layout Duck uses—reuse it when it fits, not as a series default.

### Shared-library policy (DRY)

- **Rule of two: the second use of a piece of code moves it to `shared/`.** Extract first (own commit), then use it
  from both plugins. Never copy a file from another plugin; plugins must not include other plugins' files
  (`scripts/check_repo.sh`, run in CI).
- Where: JUCE-free -> `shared/dsp`; JUCE without GUI -> `shared/core`; GUI -> `shared/ui`; test helpers ->
  `shared/testing`; build logic -> `cmake/`; scaffolding -> `templates/plugin`.
- Variation goes through seams, never copies or `#ifdef`s: `Theme`/`Palette`/`Motif`, constructor arguments,
  virtual hooks (`CurveShaperProcessor::renderCycle` etc.), templates.
- Extracting must not change other plugins: their tests pass unchanged, screenshots look the same, parameter IDs and
  ValueTree identifiers stay identical. Tests move with the code.
- The full decision checklist is in the skill (section 6). Keep this section, the layout above and the skill's reuse
  table in sync when you add or move shared code.

## Conventions

- C++17, 4-space indent, Allman braces, `.clang-format` is authoritative. Namespaces: shared code in
  `animalaudio::dsp` / `animalaudio::core` / `animalaudio::ui`; each plugin in its own lowercase namespace (`duck`).
- Put anything that can be JUCE-free in JUCE-free headers (`shared/dsp` or `Source/dsp`) so it is
  unit-testable without a GUI. Test DSP numerically (bounds, periodicity, click-freeness, block-size
  independence), not by ear.
- Parameters: `AudioProcessorValueTreeState` with `juce::ParameterID{ "id", version }`; read on the audio
  thread through `getRawParameterValue()` atomics. Non-parameter state (like Duck's curve) lives as
  children of `apvts.state` so it is saved/restored together; follow `core::CurveState` (hold the APVTS
  `state` handle by reference, because `replaceState()` reassigns it).
- `shared/ui` and `shared/core` are INTERFACE libraries: their sources are compiled into the consuming target. Do not turn
  them into STATIC libs that link JUCE modules (module sources would be compiled twice into one binary).
- Don't vendor third-party code; add pinned FetchContent entries in `cmake/FetchDependencies.cmake`.
- No comments that narrate code; comment only non-obvious intent, constraints, and trade-offs.

## Realtime-safety rules (audio thread: `processBlock` and everything it calls)

1. No heap allocation or deallocation: no `new`, `std::vector` growth, `std::string`, `juce::String`,
   `AudioBuffer` resizing, `std::function` construction, lambdas that capture-by-heap. Preallocate in
   `prepareToPlay` or the constructor.
2. No locks that can block: no `std::mutex`, `CriticalSection`, `ScopedLock`. Use atomics, or `SpinLock`
   **try-locks only** (see `CurveState::fetchTable`: if the message thread holds it, keep last block's data).
3. No I/O, logging, `DBG`, file/network access, no waiting on the message thread, no `MessageManager` calls.
4. No exceptions, no unbounded loops, no `std::pow`/`exp` in per-sample loops when a table or recurrence works
   (Duck bakes curves into a 1024-point LUT on the message thread).
5. Every gain/parameter step that can be heard must be smoothed (`OnePoleSmoother`); assume hard-edged
   curves and automation jumps.
6. Use `juce::ScopedNoDenormals`. Handle any block size (including 1) and any sample rate; no
   assumptions that the host calls `prepareToPlay` before the first block with the final layout.
7. Audio -> UI data goes through `std::atomic` (relaxed) values or lock-free FIFOs, never shared mutable
   containers. UI -> audio data is published as an immutable snapshot (see `CurveTable`).
8. The playhead may be absent: `getPlayHead()` can be null and `getPosition()` empty. Always have a
   free-run fallback (`PhaseTracker`).

## Shaper plugins (shared) and Duck specifics

The DSP/state/editor below is shared by every plugin built on `CurveShaperProcessor`/`ShaperEditor`; Duck is the first.

- Gain per sample: `target = 1 - mix * (1 - curve(phase))`, then a one-pole declick (0.7 ms); `mix` itself is
  smoothed (15 ms). `curve` is a 1024-point LUT baked from `ShapeCurve` (nodes + per-segment bend).
- Divisions are one cycle per note length: 1/8 = 0.5 beat, 1/4 = 1, 1/2 = 2, 1/1 = 4 (PPQ is in quarter notes).
  Host playing + PPQ available -> phase = frac(ppq / beats); otherwise free-run at host BPM or the
  `fallbackBpm` parameter.
- State: params `division`, `mix`, `fallbackBpm` + `<Curve preset="n"><Node x y bend/>...</Curve>` child of
  the APVTS state (`preset` -1 = custom). Presets: `dsp::standardCurvePresets(names)`; Duck's names are in
  `Source/dsp/DuckPresets.cpp` (16, keep the count).
- UI (`ui::ShaperEditor`, 820x470): left: brand badge, NAME title + mascot, MIX knob (cap from the Motif), fallback-BPM
  slider. Right: division pills + sync status, curve editor, preset nav/flip/invert, 2x8 preset grid.
  Duck's `DuckMotif`: rubber duck mascot, duck head from above as knob cap (bill = pointer), feathers, pond ripples.

## Turtle Time specifics

Turtle Time is a tempo-synced time warp (half-time, reverse, beat repeat) with its own processor and editor,
not the curve-shaper stack. The patterns follow a half-time / retrigger workflow; the DSP, names, and art
are original.

- DSP: `plugins/turtletime/Source/dsp/TurtleTimeEngine.h`. Input is written into a circular buffer at 1x.
  A pattern plays that buffer back at a signed rate (0.5 = one octave down, -1 = reverse) with 4-point
  cubic interpolation. On each musical window the read pointer snaps back to "now" (catch-up) or a captured
  slice repeats, with a ~10 ms crossfade on the jump. The window locks to host PPQ only while the transport
  is playing. Stopped, audio is passed dry (the buffer still records) and the UI phase parks on the host
  playhead; playback resumes with a snap. There is no user BPM control. History is allocated in `prepare`
  (32 seconds); `process` does not allocate. Input/output peaks per window phase go to the UI through two
  `dsp::CycleScope`s (frozen while stopped).
- Keep pitch (`keepPitch` on, only for slowed patterns, `dsp::slowsDown(rate)`: HALF and SLOW): the read head becomes
  two periodic-Hann grains (40 ms, 50% overlap, sum = 1) read at 1x whose start points follow the slow position, so time
  stretches and pitch stays. Each new grain start is nudged within ±10 ms (never past the write head) to where its
  recent past best matches the grain it overlaps (normalised cross-correlation, coarse/fine search), so tones stay in
  phase: level is flat from 50 Hz up, about ±12% wobble at 30 Hz. Window jumps and toggling crossfade like any jump.
  Tape mode (off) is unchanged. Costs about 0.5% of a core at 48 kHz stereo; tested in `TurtleTimeTests` ("keep pitch").
- Parameters (APVTS, version 1): `pattern` (32 choices, default `Half 1 Bar`), `mix` (default 100%), `keepPitch`
  (bool, default off = tape-style pitch drop). Host programs mirror the pattern list. Mix moves are smoothed (15 ms).
- Patterns are a 4×8 matrix, index = row * 8 + column. Rows: HALF (0.5x), SLOW (0.25x), REVERSE (-1x),
  REPEAT (slice loop). Columns are the same musical lengths in every row: 1/32, 1/16, 1/8, 1/4, 3/8, 1/2,
  1 bar, 2 bars (for REPEAT that is the slice; it loops across a bar, or twice its length from 1 bar up).
  Program names are `Half 1/4`, `Reverse 1 Bar`, …
- UI (`TurtleTimeEditor`, 940×528): header (hex-shell mark, TURTLE TIME / TIME WARP, brand badge); a recessed
  display with the input (grey) and output (green) envelopes over the current window, beat grid, white NOW
  line and amber READ head, readout `HALF · 0.5X SPEED · 1 OCTAVE DOWN` / `· PITCH KEPT` (`dsp::pitchLabel`); a MIX
  panel (ticked knob, % readout, PITCH DROP/KEEP segments, dimmed for 1x patterns, PLAYING/STOPPED + host BPM); the
  pattern matrix with row labels and rectangular cells. Motion comes from the host playhead, not a wall clock.
  Pattern blurbs do not mention pitch (it depends on the toggle).
- Palette: graphite with a mint-green accent (`kThemeHue` 147°); amber only for the read head. Product name in
  hosts: "Turtle Time"; VST3 bundle: `TurtleTime.vst3`.

## Branding

- Company: **Animal Audio** (wordmark "ANIMAL AUDIO" with a paw glyph, `animalaudio::ui::BrandBadge`;
  it must appear on every plugin UI). Plugin names are single animal words in caps in the UI ("DUCK").
- Tone: playful but clean. Warm, saturated accents on a dark, low-contrast base; rounded shapes; one
  motif per plugin drawn with `juce::Path` (no bitmap assets, no external fonts).
- Every plugin has its own palette and its own animal (see "Theme" above); Duck's: yellow `#FFCB1F`, bill orange `#FF8A1F`/`#FF7A1A`, charcoal `#1A1610`/`#241F15`, water blue
  `#4FB6D9` (only as faint ripples). Motifs: rubber duck, bill, feather, water ripples.
- Plugins may be *inspired by* existing tools' workflows but must not copy their branding, names, art or code.
  (Duck's layout takes cues from volume-shaper plugins; nothing else.)

## Git / PR conventions

- Branch off `main`, small focused commits, PRs to `main`. CI (`.github/workflows/build.yml`) builds and tests
  on Linux, macOS and Windows and uploads VST3 bundles as artifacts.
- Don't commit `build/`, `*.vst3`, or IDE files (see `.gitignore`). README screenshots in `docs/images/` are
  committed on purpose.

## Gotchas

- `ParameterAttachment` / `SliderParameterAttachment` callbacks run in reverse listener-registration order, i.e. possibly
  *before* the APVTS updates `getRawParameterValue()`. In UI callbacks use the value passed to the callback (or the
  parameter itself, e.g. `AudioParameterChoice::getIndex()`), never the raw atomic. This caused Duck's division pills to
  highlight one click late (regression tests in the shared suites, `shared/testing`, `divisionPills*`).
- JUCE buttons that fire on mouse *up* (`Button` default) only click if their state still says "down", and that state
  is re-read from the **OS pointer** (`isMouseOver()`) whenever focus/enablement changes mid-click. If a host's pointer
  coordinates differ from its event coordinates the click is silently dropped. Clickable shared widgets therefore use
  `setTriggeredOnMouseDown(true)` (`PillButton`, preset thumbnails). Synthetic-event tests must also move the OS pointer
  (`DesktopEditor::send` does), and a test with the pointer left elsewhere guards the regression.
- A bare Xvfb (no window manager) has no ICCCM atoms and JUCE aborts with `BadAtom`; `prepareNativeWindows()`
  (shared/testing) interns them and keeps a connection open. Test targets compile with `JUCE_USE_XSHM=0` because MIT-SHM
  `X_ShmAttach` fails intermittently under `xvfb-run`.
- `Button::triggerClick()` is asynchronous; in tests pump the message loop (`JUCE_MODAL_LOOPS_PERMITTED=1` on test targets).
