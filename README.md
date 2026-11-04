# Krita Nova: Android APK builds

GitHub Actions builds a Krita Android APK from upstream Krita plus the
patches in [`patches/`](patches). Nothing heavy lives in this repo: the runner
fetches Krita at the commit in [`KRITA_REF`](KRITA_REF) and downloads KDE's
prebuilt Android dependencies (Qt5, KF5, …) itself.

| Patch | Change |
|---|---|
| `01-eraser-tool.patch` | Eraser is its own tool instead of a brush mode (**E** toggles it) |
| `02-live-stroke-presets.patch` | Brush preset chooser (Details view) shows a live stroke and the real brush tip per preset |
| `03-touch-draggable-ui.patch` | Docker separators/splitters are wider and react to finger and stylus drags (Android) |
| `04-brush-editor-screen-fit.patch` | Floating brush settings editor is sized relative to the screen and rotation (Android) |
| `05-layers-hold-to-drag.patch` | Layers docker: press-and-hold drags a layer; a menu button on each row opens the layer menu (Android) |
| `06-screen-tracker-rotation.patch` | Fixes a SAFE ASSERT dialog when rotating the device with the brush editor open |
| `07-faster-fill-tool.patch` | Faster Fill tool: default fills use the fast path; other fills no longer paint a whole-canvas temporary layer |
| `08-settings-export-import.patch` | Settings → Export Settings… / Import Settings… (settings, shortcuts, toolbars, input profiles in one .zip) |
| `09-palm-rejection.patch` | Palm rejection: touch on the canvas is ignored while the pen is down/hovering and shortly after, plus touches larger than a fingertip (Configure Krita → General → Tools) |
| `10-editor-stroke-previews.patch` | Live stroke previews (patch 02) also in the Brush Editor and the toolbar preset popup; preset lists default to Details view |
| `11-brush-settings-title-bar.patch` | Android: brush editor panel without the white border, with a "Brush Settings" bar and a close (×) button on top |
| `12-preset-preview-quick-brush.patch` | Live stroke preview and round tip icon for Quick Brush presets (e.g. "b) Basic-1"); cleaner thumbnail fallback |
| `13-line-tool-shortcut-scope.patch` | The Line tool shortcut only works while the Brush, Eraser, Multibrush or Dynamic Brush tool is active (toolbox button unchanged) |
| `14-floating-docker-resize.patch` | Android: resize undocked dockers by dragging a wide band along their edges (taps there still reach buttons) |
| `15-android-brush-panel.patch` | Android: top bar = New/Open/Save + mirror/wrap, layouts, workspaces; Undo/Redo and the brush toolbar move into a floating narrow Brush Panel with tall vertical Size and Opacity sliders and a one-shot color sampler button (left edge on first run) |
| `16-status-items-in-top-bar.patch` | Android: image size, canvas rotation and zoom move from the bottom-right status bar to the right end of the top bar |
| `17-nova-splash-screen.patch` | Krita Nova loading screen (artwork by novaillusion); no Krita logo/banner, version and loading text at the bottom right |
| `18-app-name-nova-krita.patch` | App name shown as "Nova Krita" (Android launcher, window titles, dialogs); settings and package id unchanged |
| `19-faster-transform-tool.patch` | Faster Transform tool: multi-threaded resampling passes, bilinear filter for the low-resolution live preview (final result unchanged) |
| `20-ctrl-t-starts-transform.patch` | Ctrl+T with the Transform tool already active starts a new transform at once (no extra tap on the canvas) |
| `21-preset-preview-after-drag.patch` | Live preset previews re-render only after a slider drag ends (no lag while dragging Size/Opacity) |

## Get an APK

1. **Actions** tab → **Krita Nova Android APK** → **Run workflow**. Pick the
   ABI (`arm64-v8a` covers almost all current phones and tablets).
2. Wait. The first run takes about 2–4 h on GitHub's runners; later runs are
   much faster thanks to the compiler cache.
3. Open the finished run → **Artifacts** → download
   `krita-nova-arm64-v8a-<sha>-apk` (a zip containing the `.apk`).
4. Install it on a device with USB debugging enabled:

   ```
   adb install -r -d krita-arm64-v8a-5.4.0-prealpha-<sha>-debug.apk
   ```

   Or copy the APK to the device and open it (allow "install unknown apps").

Pushing a change to `patches/`, `build/`, `KRITA_REF` or the workflow on
`main` also starts a build.

### If the 6-hour limit is hit

GitHub stops jobs after 6 h. The build step stops a little earlier, and the
compiler cache is saved even then, so **just run the workflow again**. It picks
up where it left off.

### Public or private repo?

Use a **public** repo if you can (Krita is GPL anyway). Public repos get
4-core / 16 GB runners and unlimited minutes. Private repos get 2-core / 7 GB
runners and a monthly minute quota, which may not be enough for a full Krita
build within 6 h.

## Updating

* **New patch:** add `patches/03-something.patch` (paths relative to the
  Krita root, `a/libs/...`). Patches are applied in name order, and a patch
  that no longer applies fails the build with git's reason.
* **Newer Krita:** put a new commit SHA in `KRITA_REF`, or pass one when
  running the workflow. Patches may need rebasing.
* **Install over the previous build:** every run signs with the same debug
  key (kept in the Actions cache). If that cache ever expires, a new key is
  generated, and you'll have to uninstall the old APK once.

## Layout

```
.github/workflows/android-apk.yml   the CI job
KRITA_REF                           upstream Krita commit that is built
patches/                            Krita Nova changes, applied in order
patches/original/                   patches as originally received (not applied)
build/                              build kit, also usable locally (see build/README-local-build.md)
tests/                              unit tests run against the patched source (workflow tests.yml)
```

The same kit builds locally on Windows with Docker:
`build/build-android.ps1` (copy `build/` and `patches/` into
`krita/packaging/android/local-build/` first).
