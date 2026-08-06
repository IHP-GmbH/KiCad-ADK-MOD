#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 IHP GmbH
"""Hold the fork's delta against upstream KiCad to a declared, reviewed list.

This is a fork of KiCad carrying a small ADK delta: nanometre geometry limits,
micrometre units in the PCB tools, and the chiplet/HyperLynx exporters that the
rest of the ecosystem is built on. Everything else is upstream and must stay
byte-identical to upstream, because every upstream file we touch is a file we
have to re-merge by hand at the next sync, forever.

Nothing measured that surface. `ci/fork-delta.txt` declares it, and this check
compares the declaration against the tree. What it catches:

  - an upstream file edited in passing (a debug print, a stray formatting pass)
    that nobody meant to own, silently enlarging the merge burden;
  - a new fork file added without being declared, so the next person to sync
    has no list of what is ours;
  - a fork file deleted, which is how a feature quietly disappears.

The manifest is regenerated deliberately, in the same commit that changes the
delta, and the diff of the manifest is then the reviewable summary of what the
fork's surface just became. That is the whole point: the review artifact is the
manifest, not this script.

Usage:
    check_fork_delta.py
    check_fork_delta.py --regenerate
    check_fork_delta.py --print-base
    check_fork_delta.py --self-test
"""

import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

MANIFEST = pathlib.Path("ci/fork-delta.txt")

HEADER = """\
# The fork's entire delta against upstream KiCad.
#
# `base` is the last upstream commit this fork contains; everything after it is
# ours. Each line below is one path we own, with its status against that base:
# A added, M modified, D deleted.
#
# Every M line is a file that has to be re-merged by hand at the next upstream
# sync, so the list is meant to stay short and every entry is meant to be
# deliberate. Regenerate it in the same commit that changes the delta:
#
#     python3 ci/check_fork_delta.py --regenerate
#
# and let the manifest's own diff be the summary of what the fork's surface
# just became. See NANOMETER_MODIFICATIONS.md for what the changes do.
"""


def git(*args, cwd=None):
    return subprocess.run(
        ["git", *args], cwd=cwd, capture_output=True, text=True, check=True
    ).stdout


def parse(path):
    """Return (base sha, [(status, path), ...]) from a manifest."""
    base, entries = None, []
    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if line.startswith("base "):
            base = line.split(None, 1)[1].strip()
            continue
        if "\t" not in line:
            raise SystemExit("%s:%d: expected '<status>\\t<path>', got %r"
                             % (path, lineno, line))
        status, p = line.split("\t", 1)
        entries.append((status.strip(), p.strip()))
    if base is None:
        raise SystemExit("%s: no `base <sha>` line" % path)
    if not re.fullmatch(r"[0-9a-f]{40}", base):
        raise SystemExit(
            "%s: base must be a full 40-hex sha, got %r. An abbreviated or empty "
            "base silently resolves to something else and the comparison then "
            "passes against the wrong tree." % (path, base)
        )
    return base, sorted(entries)


def actual(base, rev="HEAD", cwd=None):
    """The tree's real delta against the base."""
    out = git("diff", "--name-status", "--no-renames", base, rev, cwd=cwd)
    entries = []
    for line in out.splitlines():
        if not line.strip():
            continue
        status, path = line.split("\t", 1)
        entries.append((status.strip(), path.strip()))
    return sorted(entries)


def render(base, entries):
    body = "".join("%s\t%s\n" % e for e in sorted(entries))
    return "%s\nbase %s\n\n%s" % (HEADER, base, body)


def check(root, rev="HEAD"):
    manifest = pathlib.Path(root) / MANIFEST
    base, declared = parse(manifest)
    failures = []

    shallow = git("rev-parse", "--is-shallow-repository", cwd=root).strip()
    if shallow == "false":
        # Only meaningful in a full clone. CI fetches the base and the tip as
        # two depth-1 tips, where ancestry is unknowable and git would answer
        # "no" for a base that is perfectly fine.
        ancestor = subprocess.run(
            ["git", "merge-base", "--is-ancestor", base, rev],
            cwd=root, capture_output=True,
        )
        if ancestor.returncode != 0:
            failures.append(
                "base %s is not an ancestor of %s. If the fork was rebased onto a "
                "newer upstream, set the new base and regenerate; the delta is "
                "meaningless against a base the history does not contain."
                % (base[:10], rev)
            )
            return failures

    found = actual(base, rev, cwd=root)
    for entry in sorted(set(found) - set(declared)):
        failures.append(
            "%s\t%s: changed against upstream but not declared in %s"
            % (entry[0], entry[1], MANIFEST)
        )
    for entry in sorted(set(declared) - set(found)):
        failures.append(
            "%s\t%s: declared in %s but no longer changed against upstream"
            % (entry[0], entry[1], MANIFEST)
        )
    return failures


def self_test():
    """Every branch above, made to fail on purpose."""
    cases = []

    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        env = ["-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false"]

        def g(*args):
            return subprocess.run(["git", *env, *args], cwd=root,
                                  capture_output=True, text=True, check=True).stdout

        g("init", "-q", "-b", "main", ".")
        (root / "upstream.txt").write_text("upstream\n")
        g("add", "-A")
        g("commit", "-qm", "base")
        base = g("rev-parse", "HEAD").strip()

        (root / "upstream.txt").write_text("upstream, edited by the fork\n")
        (root / "ours.txt").write_text("ours\n")
        (root / MANIFEST.parent).mkdir(parents=True, exist_ok=True)
        (root / MANIFEST).write_text(render(base, actual(base, "HEAD", cwd=root)))
        g("add", "-A")
        g("commit", "-qm", "fork")
        (root / MANIFEST).write_text(render(base, actual(base, "HEAD", cwd=root)))
        g("add", "-A")
        g("commit", "-qm", "declare the delta")

        good = (root / MANIFEST).read_text()

        def expect(name, want_fail):
            got = check(root)
            cases.append((name, bool(got) == want_fail, got))

        expect("a manifest matching the tree passes", False)

        (root / MANIFEST).write_text(
            good.replace("A\tours.txt\n", ""))
        expect("an undeclared fork file fails", True)

        (root / MANIFEST).write_text(
            good.replace("M\tupstream.txt\n", ""))
        expect("an undeclared edit to an upstream file fails", True)

        (root / MANIFEST).write_text(good + "A\tphantom.txt\n")
        expect("a declared file that is no longer ours fails", True)

        (root / MANIFEST).write_text(
            good.replace("M\tupstream.txt", "A\tupstream.txt"))
        expect("a wrong status fails", True)

        (root / MANIFEST).write_text(good.replace("base " + base, "base " + "0" * 40))
        expect("a base outside this history fails", True)

        (root / MANIFEST).write_text(good)

    ok = True
    for name, passed, got in cases:
        print("%-52s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            ok = False
            print("   checker returned: %r" % (got,))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--regenerate", action="store_true")
    ap.add_argument("--print-base", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--rev", default="HEAD")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    root = pathlib.Path(git("rev-parse", "--show-toplevel").strip())
    manifest = root / MANIFEST

    if args.print_base:
        print(parse(manifest)[0])
        return 0

    if args.regenerate:
        base, _ = parse(manifest)
        manifest.write_text(render(base, actual(base, args.rev, cwd=root)))
        print("wrote %s" % MANIFEST)
        return 0

    failures = check(root, args.rev)
    for f in failures:
        print("FAIL: %s" % f, file=sys.stderr)
    if failures:
        print(
            "\nThe fork's delta against upstream is not what %s declares.\n"
            "If the change is intended, regenerate the manifest in this same commit:\n"
            "    python3 ci/check_fork_delta.py --regenerate\n"
            "If it is not, the edit above is an upstream file the fork did not mean "
            "to own." % MANIFEST,
            file=sys.stderr,
        )
        return 1
    base, declared = parse(manifest)
    print("fork delta matches %s: %d paths against upstream %s"
          % (MANIFEST, len(declared), base[:10]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
