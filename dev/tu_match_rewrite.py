#!/usr/bin/env python3
"""Rewrite the tagged-union match macros into plain C++.

Applied in phases; each invocation rewrites one macro family in place:

    dev/tu_match_rewrite.py iflet FILE...   # TU_IFLET
    dev/tu_match_rewrite.py test FILE...    # TU_TEST1 / TU_TEST2 / TU_OPT1
    dev/tu_match_rewrite.py match FILE...   # TU_MATCH / TU_MATCHA / TU_MATCH_DEF
    dev/tu_match_rewrite.py hdr FILE...     # TU_MATCH_HDR(A) / TU_ARM(A)

For TU_MATCHA the union type behind the case labels is inferred from the
.tu descriptions: the set of arm tags is matched against every union's
variant set, and the rewrite fails loudly when that is ambiguous.

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
        elif c == "(":
            depth += 1
        elif c == ")":
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


def code_mask(text):
    """Spans of comments and string/char literals, to skip macro hits there."""
    spans = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            start = i
            quote = c
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    break
                i += 1
            spans.append((start, i + 1))
        elif text.startswith("//", i):
            start = i
            i = text.find("\n", i)
            if i < 0:
                i = n
            spans.append((start, i))
        elif text.startswith("/*", i):
            start = i
            i = text.find("*/", i) + 2
            spans.append((start, i))
        i += 1
    return spans


def in_mask(spans, pos):
    import bisect
    idx = bisect.bisect_right(spans, (pos, float("inf"))) - 1
    return idx >= 0 and spans[idx][0] <= pos < spans[idx][1]


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
            if not m or in_mask(code_mask(text), m.start()):
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
    offset = 0
    while True:
        m = re.compile(r"\bTU_IFLET\(").search(text, offset)
        if not m:
            return text
        if in_mask(code_mask(text), m.start()):
            offset = m.end()
            continue
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


def load_union_specs():
    """name -> frozenset(tags) for every union described in bin/rustc/*.tu."""
    specs = {}

    class StubVariant:
        def __init__(self, tag, *args, **kwargs):
            self.tag = tag

    specs["MIRLValue::Storage"] = frozenset(
        {"Argument", "Local", "Static", "Return"})
    specs["MIRLValue::Wrapper"] = frozenset(
        {"Deref", "Field", "Downcast", "Index"})
    specs["MIRLValue::RefCommon"] = frozenset(
        {"Argument", "Local", "Static", "Return",
         "Deref", "Field", "Downcast", "Index"})
    for path in pathlib.Path("bin/rustc").glob("*.tu"):
        def generate(**kwargs):
            specs[kwargs["name"]] = frozenset(v.tag for v in kwargs["variants"])

        exec(compile(path.read_text(), str(path), "exec"),
             {"generate": generate, "v": StubVariant,
              "context": lambda *_: None, "local": lambda: None})
    return specs


def infer_class(specs, tags, where, subjects=()):
    exact = [name for name, variants in specs.items() if variants == tags]
    if len(exact) == 1:
        return exact[0]
    superset = [name for name, variants in specs.items() if tags <= variants]
    if len(superset) == 1:
        return superset[0]
    candidates = set(exact or superset)
    hint = " ".join(subjects)
    if candidates == {"ASTEnumVariantData", "ASTStructData"}:
        if "str" in hint:
            return "ASTStructData"
        if "var" in hint or "enm" in hint or re.search(r"\bv\b", hint):
            return "ASTEnumVariantData"
    base = pathlib.Path(str(where)).name

    def sideOf(name):
        if name.startswith("AST") or name in ("TypeData", "TypeDataErasedTypeInner", "Ent"):
            return "ast"
        if name.startswith(("HIR", "MIR", "Typeck", "ImplRef")) or name == "PatternRule":
            return "hir"
        return None

    if base.startswith(("ast_", "synext_", "expand_", "resolve_", "parse_", "macro_")):
        file_side = "ast"
    elif base.startswith(("hir_", "mir_", "trans_")):
        file_side = "hir"
    else:
        file_side = None
    if file_side:
        filtered = [c for c in candidates if sideOf(c) in (file_side, None)]
        if len(filtered) == 1:
            return filtered[0]
        if filtered:
            candidates = set(filtered)
    # Try matching a suffix of the subject's identifiers against the
    # candidate names ("path.data" -> HIRPathData).
    idents = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", hint)
    for k in range(len(idents), 0, -1):
        key = "".join(idents[-k:]).lower()
        matched = [c for c in candidates if key in c.lower()]
        if len(matched) == 1:
            return matched[0]
    print(f"{where}: cannot infer union for tags {sorted(tags)}:"
          f" candidates {sorted(candidates)} subjects {list(subjects)}"
          " - left for hand conversion")
    return None


NO_BREAK = re.compile(r"(?:^|\n)\s*(?:return\b[^;]*|throw\b[^;]*|break|continue)\s*;\s*\Z")


# The pointer-tagged MIRLValue classes return their payloads by value, so
# their bindings copy via decltype (as the macros did) instead of auto&.
BY_VALUE_CLASSES = {"MIRLValue::Storage", "MIRLValue::Wrapper",
                    "MIRLValue::RefCommon"}


def emit_arm(lines, indent, label, subjects, names, code, used_break=True):
    lines.append(f"{indent}    case {label}: {{")
    cls = label.rsplit("::TAG_", 1)[0]
    tag = label.rsplit("TAG_", 1)[1]
    binder = ("decltype({subj}.as_{tag}())"
              if cls in BY_VALUE_CLASSES else "auto&")
    for subj, name in zip(subjects, names):
        bind = binder.format(subj=subj, tag=tag)
        lines.append(f"{indent}        {bind} {name} = {subj}.as_{tag}();")
        if not re.search(r"\b%s\b" % re.escape(name), code):
            lines.append(f"{indent}        (void){name};")
    emit_code(lines, indent + "        ", code)
    if used_break and not NO_BREAK.search(code):
        lines.append(f"{indent}        break;")
    lines.append(f"{indent}    }}")


def emit_code(lines, indent, code):
    code_lines = code.splitlines()
    rest = [cl for cl in code_lines[1:] if cl.strip()]
    common = min((len(cl) - len(cl.lstrip()) for cl in rest), default=0)
    for pos, cl in enumerate(code_lines):
        if not cl.strip():
            lines.append("")
        elif pos == 0:
            lines.append(f"{indent}{cl.strip()}")
        else:
            lines.append(f"{indent}{cl[common:].rstrip()}")


def rewrite_match(text, specs, where):
    offset = 0
    while True:
        m = re.compile(r"\bTU_MATCH(A|_DEF)?\(").search(text, offset)
        if not m:
            return text
        if in_mask(code_mask(text), m.start()):
            offset = m.end()
            continue
        kind = m.group(1) or ""
        args, end = scan_args(text, m.end() - 1)
        pos = 0
        if kind != "A":
            cls = args[pos].strip()
            pos += 1
        subjects_raw = args[pos].strip()
        names_raw = args[pos + 1].strip()
        pos += 2
        default_code = None
        if kind == "_DEF":
            default_code = args[pos].strip()
            if default_code.startswith("(") and default_code.endswith(")"):
                default_code = default_code[1:-1]
            pos += 1
        arms = []
        for arm in args[pos:]:
            arm = arm.strip()
            comments = []
            while arm.startswith("//") or arm.startswith("/*"):
                if arm.startswith("//"):
                    comment, _, arm = arm.partition("\n")
                else:
                    comment, _, arm = arm.partition("*/")
                    comment += "*/"
                comments.append(comment.strip())
                arm = arm.strip()
            if not (arm.startswith("(") and arm.endswith(")")):
                arms.append(None)
                continue
            parts, _ = scan_args(arm, 0)
            arms.append((parts[0].strip(), ",".join(parts[1:]).strip(), comments))

        def split_list(raw):
            if raw.startswith("(") and raw.endswith(")"):
                parts, endp = scan_args(raw, 0)
                if endp == len(raw):
                    return [p.strip() for p in parts]
            return [raw]

        subjects = [subject(sub) for sub in split_list(subjects_raw)]
        names = split_list(names_raw)

        if any(bad is None for bad in arms):
            print(f"{where}: malformed arm list at offset {m.start()} -"
                  " left for hand conversion")
            offset = m.end()
            continue
        if kind == "A":
            tags = frozenset(tag for tag, _, _ in arms)
            cls = infer_class(specs, tags, where)
            if cls is None:
                offset = m.end()
                continue

        indent = line_indent(text, m.start())
        lines = [f"switch ({subjects[0]}.tag()) {{"]
        for tag, code, comments in arms:
            for comment in comments:
                lines.append(f"{indent}    {comment}")
            emit_arm(lines, indent, f"{cls}::TAG_{tag}", subjects, names, code)
        if default_code is not None:
            lines.append(f"{indent}    default: {{")
            emit_code(lines, indent + "        ", default_code)
            if not NO_BREAK.search(default_code):
                lines.append(f"{indent}        break;")
            lines.append(f"{indent}    }}")
        lines.append(f"{indent}}}")
        tail = end
        if tail < len(text) and text[tail] == ";":
            tail += 1
        text = text[:m.start()] + "\n".join(lines) + text[tail:]


BIND_SIMPLE = re.compile(
    r"\**[A-Za-z_][A-Za-z0-9_]*(?:(?:\.|->)[A-Za-z_][A-Za-z0-9_]*)*\Z"
)


def block_end(text, open_brace, mask):
    """Index just past the '}' matching the '{' at open_brace, honouring
    literals and comments."""
    depth = 0
    i = open_brace
    n = len(text)
    while i < n:
        if in_mask(mask, i):
            i += 1
            continue
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise SystemExit("unbalanced block")


def skip_blank(text, i, mask):
    n = len(text)
    while i < n:
        if text[i] in " \t\n":
            i += 1
        elif text.startswith("//", i) or text.startswith("/*", i):
            for a, b in mask:
                if a == i:
                    i = b
                    break
            else:
                break
        else:
            break
    return i


def rewrite_hdr(text, specs, where):
    offset = 0
    while True:
        m = re.compile(r"\bTU_MATCH_HDRA?\(").search(text, offset)
        if not m:
            return text
        mask = code_mask(text)
        if in_mask(mask, m.start()):
            offset = m.end()
            continue
        args, end = scan_args(text, m.end() - 1)
        subjects_raw = args[0].strip()
        if subjects_raw.startswith("(") and subjects_raw.endswith(")"):
            parts, endp = scan_args(subjects_raw, 0)
            subjects = [subject(p) for p in parts] if endp == len(subjects_raw) \
                else [subject(subjects_raw)]
        else:
            subjects = [subject(subjects_raw)]

        # Collect the arms that follow, up to the construct's closing brace.
        arms = []
        i = end
        ok = True
        while True:
            i = skip_blank(text, i, mask)
            if text.startswith("}", i):
                i += 1
                break
            am = re.compile(r"TU_ARMA?\(").match(text, i)
            if not am:
                # Hand-written switch content mixed between arms (a raw
                # `default:` or extra case labels): splice it verbatim.
                depth = 0
                k = i
                while k < len(text):
                    if in_mask(mask, k):
                        k += 1
                        continue
                    c = text[k]
                    if c == "{":
                        depth += 1
                    elif c == "}":
                        if depth == 0:
                            break
                        depth -= 1
                    elif depth == 0 and re.compile(r"TU_ARMA?\(").match(text, k):
                        break
                    k += 1
                arms.append((None, None, text[i:k].rstrip()))
                i = k
                continue
            arm_args, arm_end = scan_args(text, am.end() - 1)
            if am.group(0) == "TU_ARM(":
                names = [arm_args[2].strip()]
                tag = arm_args[1].strip()
            else:
                tag = arm_args[0].strip()
                names = [a.strip() for a in arm_args[1:]]
            j = skip_blank(text, arm_end, mask)
            if text.startswith("{", j):
                k = block_end(text, j, mask)
                arms.append((tag, names, text[j + 1:k - 1]))
                i = k
                continue
            # A braceless arm holds exactly one statement, up to its `;`.
            depth = 0
            k = j
            while k < len(text):
                if in_mask(mask, k):
                    k += 1
                    continue
                c = text[k]
                if c in "([{":
                    depth += 1
                elif c in ")]}":
                    depth -= 1
                elif c == ";" and depth == 0:
                    break
                k += 1
            stmt = text[j:k + 1]
            if "{" in stmt:
                print(f"{where}: braceless arm with a block at offset {j}"
                      " - left for hand conversion")
                ok = False
                break
            arms.append((tag, names, stmt))
            i = k + 1
        if not ok:
            offset = m.end()
            continue

        cls = infer_class(specs,
                          frozenset(tag for tag, _, _ in arms
                                    if tag is not None), where, subjects)
        if cls is None:
            offset = m.end()
            continue

        indent = line_indent(text, m.start())
        lines = []
        arm_names = {n for _, names, _ in arms if names for n in names}
        collides = any(re.search(r"\b%s\b" % re.escape(n), sub)
                       for n in arm_names for sub in subjects)
        scoped = collides or any(not BIND_SIMPLE.match(sub.strip("()"))
                                 for sub in subjects)
        if scoped:
            lines.append("{")
            bound = []
            for pos, sub in enumerate(subjects):
                name = "tuMatch" if pos == 0 else f"tuMatch{pos + 1}"
                lines.append(f"{indent}    auto& {name} = {sub};")
                bound.append(name)
            subjects = bound
            inner_indent = indent + "    "
            lines.append(f"{inner_indent}switch ({subjects[0]}.tag()) {{")
        else:
            inner_indent = indent
            lines.append(f"switch ({subjects[0]}.tag()) {{")
        for tag, names, body in arms:
            if tag is None:
                for raw in body.splitlines():
                    lines.append(raw.rstrip())
                continue
            body_lines = body.splitlines()
            if body_lines and not body_lines[0].strip():
                body_lines = body_lines[1:]
            code = "\n".join(body_lines)
            rest = [bl for bl in body_lines if bl.strip()]
            common = min((len(bl) - len(bl.lstrip()) for bl in rest), default=0)
            code = "\n".join(bl[common:].rstrip() if bl.strip() else ""
                             for bl in body_lines)
            emit_arm(lines, inner_indent, f"{cls}::TAG_{tag}", subjects, names,
                     code)
        lines.append(f"{inner_indent}}}")
        if scoped:
            lines.append(f"{indent}}}")
        text = text[:m.start()] + "\n".join(lines) + text[i:]


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
        elif mode == "match":
            text = rewrite_match(text, load_union_specs(), path)
        elif mode == "hdr":
            text = rewrite_hdr(text, load_union_specs(), path)
        else:
            raise SystemExit(f"unknown mode {mode}")
        p.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
