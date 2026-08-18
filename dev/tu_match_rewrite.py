#!/usr/bin/env python3
"""Rewrite the tagged-union match macros into plain C++.

Applied in phases; each invocation rewrites one macro family in place:

    dev/tu_match_rewrite.py iflet FILE...   # TU_IFLET
    dev/tu_match_rewrite.py test FILE...    # TU_TEST1 / TU_TEST2 / TU_OPT1

The scanner understands string/char literals, comments and nested
parentheses, so macro arguments (including whole statements in TU_IFLET
bodies) are split faithfully at top-level commas.
"""

import pathlib
import re
import sys

SIMPLE_EXPR = re.compile(
    r"[A-Za-z_][A-Za-z0-9_]*"
    r"(?:(?:\.|->)[A-Za-z_][A-Za-z0-9_]*|\(\)|\[[A-Za-z0-9_.]*\])*\Z"
)


def scan_args(text, open_paren):
    """Return ([arg, ...], index_after_close) for the call whose '(' is at
    open_paren.  Args are split at top-level commas; literals and comments
    are opaque."""
    args = []
    depth = 0
    i = open_paren
    start = open_paren + 1
    n = len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            quote = c
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    break
                i += 1
        elif text.startswith("//", i):
            i = text.index("\n", i)
        elif text.startswith("/*", i):
            i = text.index("*/", i) + 1
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                args.append(text[start:i])
                return args, i + 1
        elif c == "," and depth == 1:
            args.append(text[start:i])
            start = i + 1
        i += 1
    raise SystemExit("unbalanced call")


def subject(expr):
    """The spelling of a match subject usable before `.is_X()`."""
    expr = expr.strip()
    while expr.startswith("(") and expr.endswith(")"):
        inner = expr[1:-1]
        try:
            args, end = scan_args("(" + inner + ")", 0)
        except SystemExit:
            break
        if len(args) == 1 and end == len(inner) + 2:
            expr = inner.strip()
        else:
            break
    if SIMPLE_EXPR.match(expr):
        return expr
    return f"({expr})"


def line_indent(text, pos):
    bol = text.rfind("\n", 0, pos) + 1
    indent = ""
    for c in text[bol:pos]:
        if c in " \t":
            indent += c
        else:
            break
    return indent


def rewrite_tests(text):
    changed = True
    while changed:
        changed = False
        for name in ("TU_TEST1", "TU_TEST2", "TU_OPT1"):
            m = re.search(r"\b%s\(" % name, text)
            if not m:
                continue
            args, end = scan_args(text, m.end() - 1)
            def glue(fragment):
                fragment = fragment.strip()
                if fragment.startswith((".", "->", "(", "[")):
                    return fragment
                return " " + fragment

            if name == "TU_TEST1":
                val, tag, test = args
                val = subject(val)
                tag = tag.strip()
                new = f"({val}.is_{tag}() && ({val}.as_{tag}(){glue(test)}))"
            elif name == "TU_TEST2":
                val, tag1, fld1, tag2, test = args
                val = subject(val)
                tag1 = tag1.strip()
                tag2 = tag2.strip()
                fld1 = fld1.strip()
                new = (f"({val}.is_{tag1}() && {val}.as_{tag1}(){glue(fld1)}.is_{tag2}()"
                       f" && {val}.as_{tag1}(){glue(fld1)}.as_{tag2}(){glue(test)})")
            else:
                val, tag, get = args
                val = subject(val)
                tag = tag.strip()
                new = f"({val}.is_{tag}() ? ({val}.as_{tag}(){glue(get)}) : nullptr)"
            text = text[:m.start()] + new + text[end:]
            changed = True
    return text


def rewrite_iflet(text):
    while True:
        m = re.search(r"\bTU_IFLET\(", text)
        if not m:
            return text
        args, end = scan_args(text, m.end() - 1)
        _cls, var, tag, name = (a.strip() for a in args[:4])
        body = ",".join(args[4:]).strip()
        var = subject(var)
        indent = line_indent(text, m.start())
        lines = [f"if ({var}.is_{tag}()) {{",
                 f"{indent}    auto& {name} = {var}.as_{tag}();"]
        if not re.search(r"\b%s\b" % re.escape(name), body):
            lines.append(f"{indent}    (void){name};")
        body_lines = body.splitlines()
        rest = [bl for bl in body_lines[1:] if bl.strip()]
        common = min((len(bl) - len(bl.lstrip()) for bl in rest), default=0)
        for pos, bline in enumerate(body_lines):
            if not bline.strip():
                lines.append("")
                continue
            if pos == 0:
                lines.append(f"{indent}    {bline.strip()}")
            else:
                lines.append(f"{indent}    {bline[common:].rstrip()}")
        lines.append(f"{indent}}}")
        # The macro call usually ends with `)` on the body's last line; any
        # trailing `;` after the call is redundant for a plain if.
        tail = end
        if tail < len(text) and text[tail] == ";":
            tail += 1
        text = text[:m.start()] + "\n".join(lines) + text[tail:]


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    mode = sys.argv[1]
    for path in sys.argv[2:]:
        p = pathlib.Path(path)
        text = p.read_text(encoding="utf-8")
        if mode == "test":
            text = rewrite_tests(text)
        elif mode == "iflet":
            text = rewrite_iflet(text)
        else:
            raise SystemExit(f"unknown mode {mode}")
        p.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
