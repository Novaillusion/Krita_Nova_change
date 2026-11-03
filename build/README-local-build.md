# Building Krita for Android locally (Windows + Docker)

This folder runs **the same pipeline KDE's GitLab CI uses** for Android
(`build-tools/ci-scripts/android.yml`) on your own PC, inside an Ubuntu 22.04
container. (The top-level `README.android.md` is outdated: `androidbuild.sh` no
longer exists.)

| File | Purpose |
|---|---|
| `Dockerfile` | Ubuntu 22.04, JDK 17, CMake 3.x, Android SDK 36, build-tools 36.0.0, NDK r27d (27.3.13750724), which are the versions `apk/build.gradle` asks for |
| `build-krita-android.sh` | The CI job steps: fetch KDE CI tooling → download **prebuilt** Android deps (Qt5, KF5, …) from KDE's package registry → compile Krita → `create-apk` |
| `build-android.ps1` | One-command Windows wrapper around Docker |

## What you get

A **debug-signed APK** (`krita-<abi>-5.4.0-prealpha-<sha>-debug.apk`) you can
sideload with `adb`.

> Note: the Android build uses this tree's **Qt5 / 5.4.0-prealpha** branch.
> The Qt6 build (6.1.0, the one your Windows zip came from) is not built for
> Android: `.gitlab-ci.yml` defines Qt6 deps only for Windows/Linux/macOS.

## Requirements

* Windows 10/11 with **Docker Desktop** (WSL2 backend), with drive `H:` shared
  (Settings → Resources → File sharing, if it asks).
* ~40 GB free disk for the Docker image + work volume.
* RAM: 16 GB+ assigned to WSL2. The build uses every core it sees; if it gets
  OOM-killed, lower the cores/memory ratio in `%UserProfile%\.wslconfig`:
  ```ini
  [wsl2]
  memory=16GB
  processors=8
  ```
* Internet access to `invent.kde.org`, `dl.google.com`, `maven.google.com`,
  `services.gradle.org`, `pypi.org`.

## Build

```powershell
cd "H:\krita_windows_source\krita-master\packaging\android\local-build"
powershell -ExecutionPolicy Bypass -File .\build-android.ps1
```

Options:

```powershell
.\build-android.ps1 -Abi arm64-v8a     # default: modern phones/tablets
.\build-android.ps1 -Abi x86_64        # Android Studio emulator / Chromebooks
.\build-android.ps1 -Abi armeabi-v7a   # old 32-bit devices
.\build-android.ps1 -BuildType Debug   # unoptimised, for debugging
.\build-android.ps1 -Shell             # drop into the container to poke around
.\build-android.ps1 -Rebuild           # rebuild the Docker image
.\build-android.ps1 -NoPatches         # build the source exactly as it is
```

The APK and logs land in `H:\krita_windows_source\android-out\`.

## Krita Nova patches

Every `patches\*.patch` in this folder is applied, in file-name order, to the
build's copy of the source. Your `krita-master` folder itself is never
modified.

| Patch | What it does |
|---|---|
| `01-eraser-tool.patch` | Eraser becomes its own tool instead of a brush mode (E toggles it). Already applied to `krita-master`, so the build skips it. |
| `02-live-stroke-presets.patch` | The brush preset chooser's Details view shows a live-rendered stroke and the real brush tip for each preset. Rebased from a Krita 6.0.4 patch; the original is in `patches\original\`. |

The build log shows what happened to each patch:

```
  [skipped]  01-eraser-tool.patch (already in the source tree)
  [applied]  02-live-stroke-presets.patch (9 files)
```

* **Add a patch:** drop a `NN-name.patch` (git or unified diff, paths relative
  to `krita-master`, i.e. `a/libs/...`) into `patches\`. No Docker rebuild
  is needed.
* **A patch that no longer applies** stops the build with `[FAILED]` and git's
  reason (usually the upstream code changed around it). Rebase it, or move it
  out of `patches\` to build without it.
* Patches in subfolders (like `original\`) are ignored.
* Patched files keep stable timestamps, so they are only recompiled when the
  patch or the file really changes.

Timing (rough): image build ~10 min once; first Krita build 1–3 h depending on
CPU; later builds are incremental (sources are rsynced with timestamps, ccache
and the build dir live in the `krita-android-work` Docker volume).

## Install on a device

Enable *Developer options → USB debugging* on the tablet, then:

```powershell
adb install -r -d "H:\krita_windows_source\android-out\krita-arm64-v8a-....apk"
adb logcat | findstr /i krita      # crash logs
```

`adb` ships with Android Studio (`%LocalAppData%\Android\Sdk\platform-tools`),
or use the one in the container (`-Shell`, then `adb` over the network).

## Without Docker (WSL2 Ubuntu 22.04 directly)

Install the packages from the `Dockerfile`, the SDK/NDK via `sdkmanager`, then:

```bash
export KDECI_ANDROID_SDK_ROOT=$HOME/Android/Sdk
export KDECI_ANDROID_NDK_ROOT=$HOME/Android/Sdk/ndk/27.3.13750724
KRITA_SRC=/mnt/h/krita_windows_source/krita-master WORK=$HOME/krita-android OUT=$HOME/krita-android/out \
  ./build-krita-android.sh
```

## Troubleshooting

* **`Unable to locate requested dependency in the registry`**: KDE's prebuilt
  deps for `master` were rotated or renamed. Retry later, or set
  `DEPS_BRANCH` (via `-Shell` → `export DEPS_BRANCH=...`) to the branch named
  in `.gitlab-ci.yml` → `DEPS_BRANCH_NAME_ANDROID`.
* **Toolchain file not found**: `krita-deps-management` changed layout;
  compare with the current `build-tools/ci-scripts/android.yml`.
* **Gradle / AGP errors**: AGP 8.12 needs JDK 17+. The image provides it.
* **Release (Play-Store-style) APK**: needs your own signing key; set
  `KRITACI_ANDROID_PACKAGE_TYPE=release` and add a `signingConfig` to
  `packaging/android/apk/build.gradle`.
* Start fresh: `docker volume rm krita-android-work`.
