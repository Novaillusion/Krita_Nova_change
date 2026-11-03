#!/usr/bin/env bash
#
# Local reproduction of Krita's Android CI job (build-tools/ci-scripts/android.yml).
#
# Runs inside the krita-android-env Docker image, or natively on Ubuntu 22.04
# (WSL2 is fine) with the Android SDK/NDK installed and these set:
#   KDECI_ANDROID_SDK_ROOT, KDECI_ANDROID_NDK_ROOT
#
# Environment knobs (all optional):
#   KRITA_ANDROID_ABI   arm64-v8a (default) | armeabi-v7a | x86_64
#   KRITA_SRC           read-only Krita source tree        (default: /src)
#   WORK                persistent work dir (deps, ccache)  (default: /work)
#   OUT                 where the finished APK is copied   (default: /out)
#   DEPS_BRANCH         krita-deps-management branch       (default: master)
#   KRITA_BUILD_TYPE    Release (default) | Debug | RelWithDebInfo
#   KRITA_SKIP_TESTS    1 = configure with -DBUILD_TESTING=OFF (default: 1, faster)

set -euo pipefail

ABI="${KRITA_ANDROID_ABI:-arm64-v8a}"
SRC="${KRITA_SRC:-/src}"
WORK="${WORK:-/work}"
OUT="${OUT:-/out}"
DEPS_BRANCH="${DEPS_BRANCH:-master}"
BUILD_TYPE="${KRITA_BUILD_TYPE:-Release}"
SKIP_TESTS="${KRITA_SKIP_TESTS:-1}"

case "$ABI" in
    arm64-v8a|armeabi-v7a|x86_64) ;;
    *) echo "## ERROR: unsupported ABI '$ABI' (use arm64-v8a, armeabi-v7a or x86_64)"; exit 1 ;;
esac

: "${KDECI_ANDROID_SDK_ROOT:?KDECI_ANDROID_SDK_ROOT must point at the Android SDK}"
: "${KDECI_ANDROID_NDK_ROOT:?KDECI_ANDROID_NDK_ROOT must point at the Android NDK}"

log() { echo; echo "######## $*"; echo; }

# Accept either the repo root or a folder containing krita-master/
if [ ! -f "$SRC/CMakeLists.txt" ] && [ -f "$SRC/krita-master/CMakeLists.txt" ]; then
    SRC="$SRC/krita-master"
fi
if ! grep -q 'KRITA_VERSION_STRING' "$SRC/CMakeLists.txt" 2>/dev/null; then
    echo "## ERROR: $SRC does not look like a Krita source tree"; exit 1
fi

KRITA_DIR="$WORK/krita"          # writable copy of the sources (CI_PROJECT_DIR)
CI_WORKDIR="$WORK/wd"            # KDECI_WORKDIR_PATH, must be OUTSIDE the source tree
mkdir -p "$KRITA_DIR" "$CI_WORKDIR" "$WORK/cache/$ABI" "$WORK/ccache/$ABI" "$OUT"

# ---------------------------------------------------------------------------
log "1/6  Syncing sources  $SRC -> $KRITA_DIR"
# Timestamps are preserved, so unchanged files are not rebuilt on later runs.
rsync -a --delete \
    --exclude '/.git/' \
    --exclude '/krita-deps-management/' \
    --exclude '/venv/' \
    --exclude '/*.log' \
    --exclude '/branch-corrected-deps.yml' \
    "$SRC/" "$KRITA_DIR/"

cd "$KRITA_DIR"

# A git repo is needed for the version string and the APK name suffix.
if [ ! -d .git ]; then
    git init -q
fi
# Sources on a Windows drive show up as executable; don't let that look like a change.
git config core.fileMode false
printf '%s\n' /krita-deps-management/ /venv/ '/*.log' /branch-corrected-deps.yml > .git/info/exclude

# ---------------------------------------------------------------------------
# Krita Nova patches: packaging/android/local-build/patches/*.patch
# (+ $KRITA_EXTRA_PATCHES/*.patch), applied in file-name order on top of the
# synced copy. A patch that is already part of the source tree is skipped.
# Set KRITA_SKIP_PATCHES=1 to build the source exactly as it is.
PATCH_DIRS=("$KRITA_DIR/packaging/android/local-build/patches")
[ -n "${KRITA_EXTRA_PATCHES:-}" ] && PATCH_DIRS+=("$KRITA_EXTRA_PATCHES")

if [ "${KRITA_SKIP_PATCHES:-0}" != "1" ]; then
    log "1b/6 Applying Krita Nova patches"
    shopt -s nullglob
    for dir in "${PATCH_DIRS[@]}"; do
        for patch in "$dir"/*.patch; do
            name="$(basename "$patch")"
            if git apply --check --whitespace=nowarn "$patch" 2>/dev/null; then
                # Keep timestamps stable: rsync restores the unpatched file on every
                # run, so without this every patched file would recompile each time.
                patch_mtime=$(stat -c %Y "$patch")
                mapfile -t files < <(git apply --numstat "$patch" 2>/dev/null | cut -f3)
                # (filter git's "has type 100755" noise for sources coming from a Windows drive)
                git apply --whitespace=nowarn "$patch" 2> >(grep -v 'has type 100755, expected 100644' >&2)
                for f in "${files[@]}"; do
                    if [ -f "$SRC/$f" ]; then src_mtime=$(stat -c %Y "$SRC/$f"); else src_mtime=0; fi
                    touch -d "@$(( patch_mtime > src_mtime ? patch_mtime : src_mtime ))" "$f"
                done
                echo "  [applied]  $name (${#files[@]} files)"
            elif git apply --reverse --check --whitespace=nowarn "$patch" 2>/dev/null; then
                echo "  [skipped]  $name (already in the source tree)"
            else
                echo "  [FAILED]   $name does not apply to this source tree:"
                git apply --check --whitespace=nowarn "$patch" 2>&1 | sed 's/^/             /'
                exit 1
            fi
        done
    done
    shopt -u nullglob
fi

git add -A >/dev/null
git -c user.name=krita-local-build -c user.email=krita-local-build@localhost \
    commit -q -m "local snapshot $(date -u +%Y-%m-%dT%H:%M:%SZ)" >/dev/null 2>&1 || true
export CI_COMMIT_SHORT_SHA="$(git log --pretty=%h -n 1)"

# ---------------------------------------------------------------------------
log "2/6  Fetching KDE CI tooling (branch: $DEPS_BRANCH)"
clone_or_update() {  # url dir [branch]
    local url="$1" dir="$2" branch="${3:-}"
    if [ -d "$dir/.git" ]; then
        git -C "$dir" fetch -q --depth=1 origin ${branch:+"$branch"}
        git -C "$dir" reset -q --hard FETCH_HEAD
    else
        git clone -q --depth=1 ${branch:+-b "$branch"} "$url" "$dir"
    fi
}
clone_or_update https://invent.kde.org/packaging/krita-deps-management.git krita-deps-management "$DEPS_BRANCH"
clone_or_update https://invent.kde.org/packaging/krita-ci-utilities.git    krita-deps-management/ci-utilities

# ---------------------------------------------------------------------------
log "3/6  Python virtualenv"
if [ ! -x "$WORK/venv/bin/python" ]; then
    python3 -m venv "$WORK/venv" --upgrade-deps
fi
# shellcheck disable=SC1091
. "$WORK/venv/bin/activate"
pip install -q -r krita-deps-management/requirements.txt
. "$WORK/venv/bin/activate"   # re-activate so the pip-installed meson wins
unset VIRTUAL_ENV             # don't let the venv shadow normal PATH lookups

# ---------------------------------------------------------------------------
# Same variables as .android-base + the per-ABI mixin in android.yml
export KDECI_BUILD_TYPE="$BUILD_TYPE"
export KDECI_GITLAB_SERVER="https://invent.kde.org/"
export KDECI_REPO_METADATA_PATH="$KRITA_DIR/krita-deps-management/repo-metadata"
export KDECI_COMPRESS_PACKAGES_ON_DOWNLOAD=False
export KDECI_SKIP_ECM_ANDROID_TOOLCHAIN=True
export CMAKE_TOOLCHAIN_FILE="$KRITA_DIR/krita-deps-management/tools/android-toolchain-krita.cmake"
if [ "$SKIP_TESTS" = "1" ]; then
    export KDECI_EXTRA_CMAKE_ARGS="-DHIDE_SAFE_ASSERTS=OFF -DBUILD_TESTING=OFF"
else
    export KDECI_EXTRA_CMAKE_ARGS="-DHIDE_SAFE_ASSERTS=OFF -DBUILD_TESTING=ON"
fi
export KRITACI_ANDROID_PACKAGE_TYPE=debug          # debug-signed APK, installable with adb
export KRITACI_SKIP_UPLOAD_NIGHTLY_PACKAGE=True
export KDECI_WORKDIR_PATH="$CI_WORKDIR"
export KDECI_ANDROID_ABI="$ABI"
export KDECI_PACKAGE_PROJECT="teams/ci-artifacts/krita-android-$ABI"   # prebuilt Qt5/KF5/etc.
export KDECI_CC_CACHE="$WORK/ccache/$ABI"
export KDECI_CACHE_PATH="$WORK/cache/$ABI"
export KRITACI_ARCHIVE_ARTIFACTS=False
export KRITACI_SKIP_COMMON_ARTIFACTS=False
export ANDROID_HOME="${ANDROID_HOME:-$KDECI_ANDROID_SDK_ROOT}"
export ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$KDECI_ANDROID_SDK_ROOT}"

if [ ! -f "$CMAKE_TOOLCHAIN_FILE" ]; then
    echo "## ERROR: toolchain file not found: $CMAKE_TOOLCHAIN_FILE"
    echo "##        krita-deps-management may have changed; check its tools/ folder."
    exit 1
fi

# ---------------------------------------------------------------------------
log "4/6  Generating dependency list"
python3 -u krita-deps-management/tools/replace-branch-in-seed-file.py \
    krita-deps-management/latest/krita-deps.yml -p -o branch-corrected-deps.yml -d "$DEPS_BRANCH"
python3 -u krita-deps-management/tools/generate-deps-file.py \
    -s branch-corrected-deps.yml -o .kde-ci.yml

# ---------------------------------------------------------------------------
log "5/6  Downloading prebuilt deps + compiling Krita for $ABI ($BUILD_TYPE)"
python3 -u krita-deps-management/ci-utilities/run-ci-build.py \
    --project krita --branch master \
    --platform "Android/$ABI/Qt5/Shared" \
    --only-build --skip-publishing 2>&1 | tee "$OUT/build-krita-$ABI.log"

# ---------------------------------------------------------------------------
# Qt 5's qmlimportscanner can segfault on Krita's QML, which makes androiddeployqt
# fail with "Invalid json output from qmlimportscanner". Put a wrapper in front of
# it that falls back to per-file scanning and names the offending files.
KIT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QIS="$CI_WORKDIR/krita/_install/bin/qmlimportscanner"
if [ -f "$KIT_DIR/qmlimportscanner-wrapper.py" ] && [ -x "$QIS" ]; then
    if ! grep -q "krita-nova: qmlimportscanner wrapper" "$QIS" 2>/dev/null; then
        mv -f "$QIS" "$QIS.real"      # fresh deps: keep the real scanner next to the wrapper
    fi
    cp "$KIT_DIR/qmlimportscanner-wrapper.py" "$QIS"
    chmod +x "$QIS"
    echo "## qmlimportscanner wrapper installed ($QIS)"
    # diagnostics for the bundled scanner, which segfaults on GitHub's runners
    # (diagnostics only: must never fail the build, hence the "|| true")
    { file "$QIS.real"; ldd "$QIS.real" 2>&1 | grep -iE "qt|not found" | head -20; } 2>/dev/null | sed 's/^/##   /' || true
    for host in "${KRITA_HOST_QMLIMPORTSCANNER:-}" /usr/lib/qt5/bin/qmlimportscanner; do
        [ -n "$host" ] && [ -x "$host" ] && { echo "## host qmlimportscanner available: $host"; break; }
    done
fi

# ---------------------------------------------------------------------------
log "6/6  Packaging APK"
python3 -u build-tools/ci-scripts/build-android-package.py 2>&1 | tee "$OUT/build-apk-$ABI.log"

shopt -s nullglob
apks=("$CI_WORKDIR/krita/_packaging/"*.apk)
if [ ${#apks[@]} -eq 0 ]; then
    echo "## ERROR: no APK produced; see $OUT/build-apk-$ABI.log"; exit 1
fi
cp -v "${apks[@]}" "$OUT/"

log "DONE. APK(s) in $OUT:"
ls -lh "$OUT"/*.apk
echo
echo "Install on a device with USB debugging enabled:"
echo "  adb install -r -d <apk>"
