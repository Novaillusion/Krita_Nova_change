#!/usr/bin/env python3
# krita-nova: qmlimportscanner wrapper
#
# androiddeployqt runs Qt's qmlimportscanner over the whole Krita source tree
# (qml-root-path = CMAKE_SOURCE_DIR) to find the QML modules to bundle. Qt 5's
# scanner can crash (segfault) on QML it does not understand, and androiddeployqt
# then aborts with "Invalid json output from qmlimportscanner".
#
# This wrapper tries, in order:
#   1. the scanner shipped with KDE's Android Qt (qmlimportscanner.real)
#   2. a host Qt 5 scanner ($KRITA_HOST_QMLIMPORTSCANNER, or Ubuntu's
#      qtdeclarative5-dev-tools build in /usr/lib/qt5/bin)
# on the whole tree, and uses the first one that produces valid JSON. If all of
# them crash, it scans the .qml/.js files one at a time with each scanner, merges
# the results and reports every file that could not be scanned, so the APK still
# gets built and the problem is visible in the build log.

import json
import os
import subprocess
import sys

MARKER = "krita-nova: qmlimportscanner wrapper"  # used by build-krita-android.sh
REAL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "qmlimportscanner.real")
HOST_CANDIDATES = [
    os.environ.get("KRITA_HOST_QMLIMPORTSCANNER", ""),
    "/usr/lib/qt5/bin/qmlimportscanner",
    "/usr/lib/x86_64-linux-gnu/qt5/bin/qmlimportscanner",
]
SCAN_EXTENSIONS = (".qml", ".js", ".mjs")


def log(msg):
    print("[qmlimportscanner-wrapper] " + msg, file=sys.stderr, flush=True)


def scanners():
    found = [REAL]
    for path in HOST_CANDIDATES:
        if path and os.access(path, os.X_OK) and os.path.realpath(path) not in map(os.path.realpath, found):
            found.append(path)
    return found


def run(args, scanner=REAL):
    try:
        proc = subprocess.run([scanner] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except OSError as e:
        proc = subprocess.CompletedProcess([scanner] + args, 127, b"", str(e).encode())
    try:
        data = json.loads(proc.stdout.decode("utf-8", "replace") or "null")
    except ValueError:
        data = None
    ok = proc.returncode == 0 and isinstance(data, list)
    return ok, data, proc


def split_args(argv):
    """Separate root paths / qml files from the remaining options (import paths, qrc files...)."""
    roots, files, rest = [], [], []
    target = roots
    for arg in argv:
        if arg.startswith("-") and arg != "-":
            if arg == "-rootPath":
                target = roots
            elif arg == "-qmlFiles":
                target = files
            else:
                rest.append(arg)
                target = rest
            continue
        target.append(arg)
    return roots, files, rest


def files_under(root):
    for dirpath, dirnames, filenames in os.walk(root):
        # skip hidden dirs (.git ...) and symlinked dirs, like the real scanner should
        dirnames[:] = sorted(d for d in dirnames
                             if not d.startswith(".") and not os.path.islink(os.path.join(dirpath, d)))
        for name in sorted(filenames):
            path = os.path.join(dirpath, name)
            if name.endswith(SCAN_EXTENSIONS) and not os.path.islink(path):
                yield path


def main():
    argv = sys.argv[1:]
    available = scanners()

    for scanner in available:
        ok, data, proc = run(argv, scanner)
        if ok:
            if scanner != REAL:
                log("using host scanner %s (%d imports)" % (scanner, len(data)))
            sys.stdout.write(proc.stdout.decode("utf-8", "replace"))
            sys.stderr.write(proc.stderr.decode("utf-8", "replace"))
            return 0
        log("%s failed on the whole tree (exit code %d, %s)"
            % (scanner, proc.returncode, "valid JSON" if isinstance(data, list) else "invalid JSON"))
        err = proc.stderr.decode("utf-8", "replace").strip()
        if err:
            log("  stderr: " + err[:500])

    log("command: qmlimportscanner " + " ".join(argv))
    log("falling back to per-file scanning with: " + ", ".join(available))

    roots, files, rest = split_args(argv)
    candidates = list(files)
    for root in roots:
        candidates.extend(files_under(root))
    log("scanning %d files under %s" % (len(candidates), ", ".join(roots) or "(no root paths)"))

    merged, seen, crashed = [], set(), []
    for path in candidates:
        for scanner in available:
            ok, data, proc = run(["-qmlFiles", path] + rest, scanner)
            if ok:
                break
        if not ok:
            crashed.append((path, proc.returncode))
            continue
        for entry in data:
            key = json.dumps(entry, sort_keys=True)
            if key not in seen:
                seen.add(key)
                merged.append(entry)

    for path, code in crashed:
        log("SKIPPED (scanner exit code %d): %s" % (code, path))
    log("%d files scanned, %d skipped, %d imports found" % (len(candidates), len(crashed), len(merged)))
    if candidates and len(crashed) == len(candidates):
        log("WARNING: no file could be scanned - the APK will lack Qt Quick modules, "
            "so Krita's QML panels (text properties, glyph palette...) will not load")

    json.dump(merged, sys.stdout, indent=1)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
