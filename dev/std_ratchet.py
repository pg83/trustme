#!/usr/bin/env python3
"""Ratchet gate for the std:: -> stl:: migration.

    std_ratchet.py --baseline PATH [--stamp PATH] FILE...

Counts banned constructs (heap ownership and std:: containers - see
PATTERNS) across all given source files and compares their total with the
checked-in baseline. Any increase fails the gate: new code must use the libstd idiom
(ObjPool ownership, interned immutable data, pool node lists) regardless
of the style of the surrounding legacy code. Any decrease rewrites the
baseline in place, so improvements lock in immediately.

The build node passes the same glob it declares as inputs; the script
never walks the tree itself.

Escape hatch, for dire necessity only: a `// escape: <reason>` comment on
the same line or an adjacent line exempts that line's hits from the
count. The reason is mandatory.
"""

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



def digit_separator(text, i):
    """True when the quote at `i` is a C++14 digit separator, not a quote.

    A separator lives inside a pp-number: the token it interrupts starts
    with a digit (or a dot and a digit), and a digit or nondigit follows
    it. The character literal prefixes - L, u, U, u8 - all start with a
    letter, so a prefixed literal never looks like a separator.
    """
    nxt = text[i + 1:i + 2]
    if not (nxt.isalnum() or nxt == "_"):
        return False
    j = i
    while j > 0 and (text[j - 1].isalnum() or text[j - 1] in "_.'"):
        j -= 1
    if text[j].isdigit():
        return True
    return text[j] == "." and text[j + 1:j + 2].isdigit()


def raw_string_prefix(text, i):
    """True when the `R"` at `i` opens a raw string.

    Only L, u, U and u8 may stand in front of the R; any other letter
    before it makes the R the tail of an identifier that merely happens
    to precede a string.
    """
    j = i
    while j > 0 and (text[j - 1].isalnum() or text[j - 1] == "_"):
        j -= 1
    return text[j:i] in ("", "L", "u", "U", "u8")


def literal_end(text, i):
    """Index past the quote closing the literal at `i`, or None.

    None means the literal never closes on its line: a quote left over
    from a construct this lexer does not know about. Swallowing text up
    to the next quote in the file would blank hundreds of lines of real
    code, so the caller keeps it as code instead.
    """
    quote = text[i]
    j, n = i + 1, len(text)
    while j < n:
        c = text[j]
        if c == "\\":
            j += 2           # any escape, `\'` and an end-of-line one alike
        elif c == quote:
            return j + 1
        elif c == "\n":
            return None
        else:
            j += 1
    return None


def blanked(text):
    """`text` with every character but its newlines turned into a space."""
    return re.sub(r"[^\n]", " ", text)


def strip_code(text):
    """Blank out comments, string and char literals (newlines kept)."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and text[i + 1:i + 2] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(blanked(text[i:j]))
            i = j
        elif c == "/" and text[i + 1:i + 2] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(blanked(text[i:j]))
            i = j
        elif (c == "R" and text[i + 1:i + 2] == '"'
              and raw_string_prefix(text, i)):
            m = re.match(r'R"([^(]*)\(', text[i:])
            if not m:
                out.append(c)
                i += 1
                continue
            close = ")" + m.group(1) + '"'
            j = text.find(close, i + m.end())
            j = n if j < 0 else j + len(close)
            out.append(blanked(text[i:j]))
            i = j
        elif c == "'" and digit_separator(text, i):
            out.append(c)
            i += 1
        elif c in "\"'":
            j = literal_end(text, i)
            if j is None:
                out.append(c)
                i += 1
                continue
            out.append(blanked(text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


ESCAPE = re.compile(r"//\s*escape:\s*\S")

RATCHET_HINT = """\
## The ratchet

`dev/std_ratchet.py` (run by the `style` gate) counts banned `std::`
constructs across `bin/rustc` against the single total in
`dev/std_ratchet.baseline`. The total may only
go down: any increase fails the gate; any decrease rewrites the baseline
automatically, locking the improvement in. Never edit the baseline
upward by hand.

Escape hatch — for dire necessity only. The rules are not absolute:
when a banned construct is truly unavoidable (FFI shape, a std::
interface you do not control, a measured perf exception), annotate it
with a `// escape: <why>` comment on the same line or an adjacent line
and the ratchet skips that line, so the counter grows only where the
necessity is written down. The reason is mandatory; an escape without a
real justification is a review reject. Reach for the pool/interning
idiom first — an escape is the last resort, not an alternative.
"""


def escaped_lines(text):
    """Line indices exempted by `// escape: why` markers (marker line +-1)."""
    out = set()
    for i, line in enumerate(text.splitlines()):
        if ESCAPE.search(line):
            out.update((i - 1, i, i + 1))
    return out


def scan(files):
    total = 0
    escaped = 0
    for path in files:
        with open(path, errors="replace") as fh:
            raw = fh.read()
        lines = strip_code(raw).splitlines()
        exempt = escaped_lines(raw)
        for label, rx in PATTERNS:
            hits = 0
            for i, line in enumerate(lines):
                n = len(rx.findall(line))
                if i in exempt:
                    escaped += n
                else:
                    hits += n
            total += hits
    return total, escaped


def load_baseline(path):
    try:
        fh = open(path)
    except FileNotFoundError:
        return None
    with fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            return int(line)
    raise ValueError(f"empty baseline: {path}")


def save_baseline(path, total):
    with open(path, "w") as fh:
        fh.write("# Managed by dev/std_ratchet.py; regenerated whenever the"
                 " total shrinks.\n")
        fh.write("# Never edit upward: new code uses the libstd idiom"
                 " (see CLAUDE.md).\n")
        fh.write(f"{total}\n")


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

    current, escaped = scan(sorted(args))
    suffix = f", {escaped} escaped" if escaped else ""
    baseline = load_baseline(baseline_path)

    if baseline is None:
        save_baseline(baseline_path, current)
        print(f"std_ratchet: baseline created with {current} hits{suffix}")
    else:
        if current > baseline:
            print(RATCHET_HINT, file=sys.stderr, end="")
            print(f"Total increased: {baseline} -> {current}", file=sys.stderr)
            return 1
        if current < baseline:
            save_baseline(baseline_path, current)
            print(f"std_ratchet: tightened {baseline} -> {current} hits{suffix}")
        else:
            print(f"std_ratchet: OK ({current} hits{suffix})")

    if stamp:
        with open(stamp, "w"):
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
