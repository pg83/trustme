#!/usr/bin/env python3
"""Ratchet gate for the std:: -> stl:: migration.

    std_ratchet.py --baseline PATH [--stamp PATH] FILE...

Counts banned constructs (heap ownership and std:: containers - see
PATTERNS) per given source file and compares them with the checked-in
baseline. Any increase fails the gate: new code must use the libstd idiom
(ObjPool ownership, interned immutable data, pool node lists) regardless
of the style of the surrounding legacy code. Any decrease rewrites the
baseline in place, so improvements lock in immediately.

The build node passes the same glob it declares as inputs; the script
never walks the tree itself.
"""

import os
import re
import sys

# (label, regex over comment/string-stripped code)
PATTERNS = [
    ("unique_ptr", re.compile(r"\bunique_ptr\b")),
    ("shared_ptr", re.compile(r"\bshared_ptr\b")),
    ("make_unique", re.compile(r"\bmake_unique\b")),
    ("make_shared", re.compile(r"\bmake_shared\b")),
    ("new", re.compile(r"\bnew\b(?!\s*\()")),  # raw heap new; placement new is pool machinery
    ("std::vector", re.compile(r"\bstd\s*::\s*vector\b")),
    ("std::map", re.compile(r"\bstd\s*::\s*(?:multi)?map\b")),
    ("std::set", re.compile(r"\bstd\s*::\s*(?:multi)?set\b")),
    ("std::unordered", re.compile(r"\bstd\s*::\s*unordered_\w+\b")),
    ("std::deque", re.compile(r"\bstd\s*::\s*deque\b")),
    ("std::list", re.compile(r"\bstd\s*::\s*list\b")),
    ("std::function", re.compile(r"\bstd\s*::\s*function\b")),
    ("std::string", re.compile(r"\bstd\s*::\s*string\b")),
]



def strip_code(text):
    """Blank out comments, string and char literals (newlines kept)."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c == "R" and text[i:i + 2] == 'R"':
            m = re.match(r'R"([^(]*)\(', text[i:])
            if not m:
                out.append(c)
                i += 1
                continue
            close = ")" + m.group(1) + '"'
            j = text.find(close, i + m.end())
            j = n if j < 0 else j + len(close)
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def baseline_key(path):
    """Stable key independent of where the build ran from."""
    norm = path.replace(os.sep, "/")
    i = norm.rfind("bin/rustc/")
    return norm[i:] if i >= 0 else os.path.basename(norm)


def scan(files):
    counts = {}
    for path in files:
        with open(path, errors="replace") as fh:
            code = strip_code(fh.read())
        for label, rx in PATTERNS:
            hits = len(rx.findall(code))
            if hits:
                counts[(baseline_key(path), label)] = hits
    return counts


def load_baseline(path):
    counts = {}
    if not os.path.exists(path):
        return None
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            count, label, rel = line.split("\t")
            counts[(rel, label)] = int(count)
    return counts


def save_baseline(path, counts):
    with open(path, "w") as fh:
        fh.write("# Managed by dev/std_ratchet.py; regenerated whenever the"
                 " counts shrink.\n")
        fh.write("# Never edit upward: new code uses the libstd idiom"
                 " (see CLAUDE.md).\n")
        for (rel, label), count in sorted(counts.items()):
            fh.write(f"{count}\t{label}\t{rel}\n")


def main():
    args = [a for a in sys.argv[1:]]
    stamp = None
    baseline_path = None
    if "--stamp" in args:
        i = args.index("--stamp")
        stamp = args[i + 1]
        del args[i:i + 2]
    if "--baseline" in args:
        i = args.index("--baseline")
        baseline_path = args[i + 1]
        del args[i:i + 2]
    if baseline_path is None or not args:
        raise SystemExit(__doc__)

    current = scan(sorted(args))
    baseline = load_baseline(baseline_path)

    if baseline is None:
        save_baseline(baseline_path, current)
        print(f"std_ratchet: baseline created with {sum(current.values())}"
              f" hits in {len(current)} entries")
    else:
        worse = [(k, baseline.get(k, 0), v) for k, v in sorted(current.items())
                 if v > baseline.get(k, 0)]
        if worse:
            print("std_ratchet: banned std:: constructs increased —"
                  " use the libstd idiom (ObjPool ownership, interning;"
                  " see CLAUDE.md):", file=sys.stderr)
            for (rel, label), base, cur in worse:
                print(f"  {rel}: {label} {base} -> {cur}", file=sys.stderr)
            return 1
        if current != baseline:
            save_baseline(baseline_path, current)
            print(f"std_ratchet: tightened"
                  f" {sum(baseline.values())} -> {sum(current.values())} hits,"
                  f" {len(baseline)} -> {len(current)} entries")
        else:
            print(f"std_ratchet: OK ({sum(current.values())} hits in"
                  f" {len(current)} entries)")

    if stamp:
        with open(stamp, "w"):
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
