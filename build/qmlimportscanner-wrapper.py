#!/usr/bin/env python3
# krita-nova: qmlimportscanner wrapper
#
# androiddeployqt runs Qt's qmlimportscanner over the whole Krita source tree
# (qml-root-path = CMAKE_SOURCE_DIR) to find the QML modules to bundle. Qt 5's
# scanner can crash (segfault) on QML it does not understand, and androiddeployqt
# then aborts with "Invalid json output from qmlimportscanner".
#
# This wrapper runs the real scanner (qmlimportscanner.real). If it crashes or its
# output is not valid JSON, the wrapper scans the .qml/.js files one at a time,
# merges the results, and reports every file that crashes the scanner on stderr,
# so the APK still gets built and the culprit is visible in the build log.

import json
import os
import subprocess
import sys

MARKER = "krita-nova: qmlimportscanner wrapper"  # used by build-krita-android.sh
REAL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "qmlimportscanner.real")
SCAN_EXTENSIONS = (".qml", ".js", ".mjs")


def log(msg):
    print("[qmlimportscanner-wrapper] " + msg, file=sys.stderr, flush=True)


def run(args):
    proc = subprocess.run([REAL] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
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
    ok, data, proc = run(argv)
    if ok:
        sys.stdout.write(proc.stdout.decode("utf-8", "replace"))
        sys.stderr.write(proc.stderr.decode("utf-8", "replace"))
        return 0

    log("real scanner failed (exit code %d, %s); falling back to per-file scanning"
        % (proc.returncode, "valid JSON" if isinstance(data, list) else "invalid JSON"))
    log("command: qmlimportscanner " + " ".join(argv))

    roots, files, rest = split_args(argv)
    candidates = list(files)
    for root in roots:
        candidates.extend(files_under(root))
    log("scanning %d files under %s" % (len(candidates), ", ".join(roots) or "(no root paths)"))

    merged, seen, crashed = [], set(), []
    for path in candidates:
        ok, data, proc = run(["-qmlFiles", path] + rest)
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

    json.dump(merged, sys.stdout, indent=1)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
