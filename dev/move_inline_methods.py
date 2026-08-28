#!/usr/bin/env python3
"""Move multi-line non-local .cpp class member definitions out of class scope.

This is deliberately a mechanical migration helper, not a C++ parser.  It
ignores function-local classes, leaves declarations in place, and appends
qualified definitions at translation-unit scope.  The compiler and style.py
are expected to finish the job for unusual declarators.
"""

import argparse
import dataclasses
import re
import sys
from pathlib import Path


IDENT = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")
MULTI_PUNCT = ("...", "::", "->", "&&", "||", "[[", "]]", "<=", ">=", "==", "!=")
DECL_ONLY_PREFIX = {"explicit", "friend", "static", "virtual"}
DECL_ONLY_SUFFIX = {"final", "override"}
LEADING_FUNCTION_SPECIFIERS = {
    "consteval",
    "constexpr",
    "explicit",
    "friend",
    "inline",
    "static",
    "virtual",
}
NON_NAMES = {
    "alignas",
    "alignof",
    "catch",
    "decltype",
    "for",
    "if",
    "noexcept",
    "requires",
    "sizeof",
    "static_assert",
    "switch",
    "typeid",
    "while",
}


@dataclasses.dataclass(frozen=True)
class Token:
    value: str
    start: int
    end: int
    line: int


@dataclasses.dataclass
class ClassInfo:
    open_index: int
    close_index: int
    name: str | None
    qualified_name: str | None
    local: bool
    template_prefixes: tuple[str, ...]


@dataclasses.dataclass
class Method:
    klass: ClassInfo
    start_index: int
    body_open_index: int
    body_close_index: int
    name_start_index: int
    parameter_open_index: int
    parameter_close_index: int
    initializer_colon_index: int | None


def lex(text: str) -> list[Token]:
    tokens = []
    i = 0
    line = 1
    line_has_code = False
    while i < len(text):
        c = text[i]
        if c in " \t\r":
            i += 1
            continue
        if c == "\n":
            line += 1
            line_has_code = False
            i += 1
            continue
        if c == "#" and not line_has_code:
            start_line = line
            j = i
            while True:
                newline = text.find("\n", j)
                if newline < 0:
                    i = len(text)
                    break
                line += 1
                continued = newline > i and text[newline - 1] == "\\"
                i = newline + 1
                j = i
                if not continued:
                    break
            line_has_code = False
            continue
        if text.startswith("//", i):
            newline = text.find("\n", i + 2)
            i = len(text) if newline < 0 else newline
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = len(text) if end < 0 else end + 2
            line += text.count("\n", i, end)
            line_has_code = not (end > 0 and text[end - 1] == "\n")
            i = end
            continue
        if c == "R" and i + 1 < len(text) and text[i + 1] == '"':
            match = re.match(r'R"([^ ()\\\t\r\n]*)\(', text[i:])
            if match:
                close = ")" + match.group(1) + '"'
                end = text.find(close, i + match.end())
                end = len(text) if end < 0 else end + len(close)
                tokens.append(Token(text[i:end], i, end, line))
                line += text.count("\n", i, end)
                line_has_code = True
                i = end
                continue
        if c == "'":
            cursor = i - 1
            while cursor >= 0 and (text[cursor].isalnum() or text[cursor] == "_"):
                cursor -= 1
            numeric_prefix = text[cursor + 1:i]
            if (
                numeric_prefix
                and numeric_prefix[0].isdigit()
                and i + 1 < len(text)
                and text[i + 1].isalnum()
            ):
                i += 1
                line_has_code = True
                continue
        if c in "\"'":
            quote = c
            j = i + 1
            while j < len(text):
                if text[j] == "\\":
                    j += 2
                elif text[j] == quote:
                    j += 1
                    break
                else:
                    j += 1
            tokens.append(Token(text[i:j], i, j, line))
            line += text.count("\n", i, j)
            line_has_code = True
            i = j
            continue
        match = IDENT.match(text, i)
        if match:
            tokens.append(Token(match.group(), i, match.end(), line))
            i = match.end()
            line_has_code = True
            continue
        punct = next((value for value in MULTI_PUNCT if text.startswith(value, i)), None)
        if punct is None:
            punct = c
        tokens.append(Token(punct, i, i + len(punct), line))
        i += len(punct)
        line_has_code = True
    return tokens


def pairs(tokens: list[Token], opening: str, closing: str) -> dict[int, int]:
    stack = []
    out = {}
    for index, token in enumerate(tokens):
        if token.value == opening:
            stack.append(index)
        elif token.value == closing and stack:
            other = stack.pop()
            out[other] = index
            out[index] = other
    return out


def named_braces(text: str, tokens: list[Token]):
    namespaces = {}
    classes = {}
    class_keywords = {}
    for index, token in enumerate(tokens):
        if token.value == "namespace":
            names = []
            cursor = index + 1
            while cursor < len(tokens) and tokens[cursor].value not in ("{", ";"):
                if IDENT.fullmatch(tokens[cursor].value) and tokens[cursor].value != "inline":
                    names.append(tokens[cursor].value)
                cursor += 1
            if cursor < len(tokens) and tokens[cursor].value == "{":
                namespaces[cursor] = names
        if token.value not in ("class", "struct"):
            continue
        if index and tokens[index - 1].value == "enum":
            continue
        if (
            token.value == "class"
            and index + 2 < len(tokens)
            and IDENT.fullmatch(tokens[index + 1].value)
            and tokens[index + 2].value in (",", "...", "=", ">")
        ):
            continue
        cursor = index + 1
        name = None
        declared_qualified = None
        specialization = None
        while cursor < len(tokens) and tokens[cursor].value not in ("{", ";"):
            value = tokens[cursor].value
            if IDENT.fullmatch(value) and value not in ("alignas", "final"):
                name = value
                break
            cursor += 1
        if name is None:
            continue
        declaration_start = cursor
        declaration_end = cursor + 1
        qualified = False
        while (
            declaration_end + 1 < len(tokens)
            and tokens[declaration_end].value == "::"
            and IDENT.fullmatch(tokens[declaration_end + 1].value)
        ):
            qualified = True
            name = tokens[declaration_end + 1].value
            declaration_end += 2
        if declaration_end < len(tokens) and tokens[declaration_end].value == "<":
            depth = 0
            specialization_end = declaration_end
            while specialization_end < len(tokens):
                if tokens[specialization_end].value == "<":
                    depth += 1
                elif tokens[specialization_end].value == ">":
                    depth -= 1
                    if depth == 0:
                        specialization_end += 1
                        break
                specialization_end += 1
            specialization = text[
                tokens[declaration_end].start:tokens[specialization_end - 1].end
            ]
            declaration_end = specialization_end
        if qualified:
            declared_qualified = text[
                tokens[declaration_start].start:tokens[declaration_end - 1].end
            ]
        cursor = declaration_end
        while cursor < len(tokens) and tokens[cursor].value not in ("{", ";"):
            cursor += 1
        if cursor < len(tokens) and tokens[cursor].value == "{":
            classes[cursor] = (name, declared_qualified, specialization)
            class_keywords[cursor] = index
    return namespaces, classes, class_keywords


def preceding_template(text: str, tokens: list[Token], class_keyword: int):
    cursor = class_keyword - 1
    if cursor < 0 or tokens[cursor].value != ">":
        return None
    close = cursor
    depth = 0
    while cursor >= 0:
        if tokens[cursor].value == ">":
            depth += 1
        elif tokens[cursor].value == "<":
            depth -= 1
            if depth == 0:
                if cursor == 0 or tokens[cursor - 1].value != "template":
                    return None
                parameter_tokens = tokens[cursor + 1:close]
                parts = []
                start = 0
                nested = 0
                for index, token in enumerate(parameter_tokens):
                    if token.value == "<":
                        nested += 1
                    elif token.value == ">":
                        nested -= 1
                    elif token.value == "," and nested == 0:
                        parts.append(parameter_tokens[start:index])
                        start = index + 1
                parts.append(parameter_tokens[start:])
                arguments = []
                for part in parts:
                    before_default = []
                    nested = 0
                    for token in part:
                        if token.value in ("<", "(", "["):
                            nested += 1
                        elif token.value in (">", ")", "]"):
                            nested -= 1
                        elif token.value == "=" and nested == 0:
                            break
                        before_default.append(token)
                    identifiers = [
                        token.value
                        for token in before_default
                        if IDENT.fullmatch(token.value)
                        and token.value not in ("class", "const", "template", "typename", "volatile")
                    ]
                    if identifiers:
                        argument = identifiers[-1]
                        if any(token.value == "..." for token in before_default):
                            argument += "..."
                        arguments.append(argument)
                prefix = text[tokens[cursor - 1].start:tokens[close].end].strip()
                return prefix, arguments
        cursor -= 1
    return None


def structural_info(text: str, tokens: list[Token]):
    brace_pairs = pairs(tokens, "{", "}")
    paren_pairs = pairs(tokens, "(", ")")
    namespaces, class_names, class_keywords = named_braces(text, tokens)
    enclosing = [None] * len(tokens)
    parent = {}
    classes = {}
    stack = []

    for index, token in enumerate(tokens):
        enclosing[index] = stack[-1][0] if stack else None
        if token.value == "{":
            parent[index] = stack[-1][0] if stack else None
            if index in namespaces:
                kind = "namespace"
                data = namespaces[index]
            elif index in class_names:
                kind = "class"
                data = class_names[index]
            else:
                kind = "block"
                data = None

            if kind == "class":
                class_parts = []
                namespace_parts = []
                local = False
                own_template = preceding_template(text, tokens, class_keywords[index])
                template_prefixes = []
                for _, scope_kind, scope_data in stack:
                    if scope_kind == "block":
                        local = True
                    elif scope_kind == "namespace":
                        namespace_parts.extend(scope_data)
                    elif scope_kind == "class":
                        outer = classes[_]
                        if outer.qualified_name:
                            class_parts = [outer.qualified_name]
                        local = local or outer.local
                        template_prefixes = list(outer.template_prefixes)
                name, declared_qualified, specialization = class_names[index]
                segment = name
                if segment and specialization is not None:
                    segment += specialization
                elif segment and own_template is not None:
                    segment += "<" + ", ".join(own_template[1]) + ">"
                if own_template is not None:
                    template_prefixes.append(own_template[0])
                if declared_qualified:
                    parts = [*namespace_parts, declared_qualified]
                else:
                    parts = [*(class_parts or namespace_parts), *([segment] if segment else [])]
                classes[index] = ClassInfo(
                    open_index=index,
                    close_index=brace_pairs.get(index, index),
                    name=name,
                    qualified_name="::".join(parts) if name else None,
                    local=local,
                    template_prefixes=tuple(template_prefixes),
                )
            stack.append((index, kind, data))
        elif token.value == "}" and stack:
            stack.pop()
    return brace_pairs, paren_pairs, enclosing, parent, classes, class_names


def direct_indices(enclosing, class_open: int, begin: int, end: int):
    return [index for index in range(begin, end) if enclosing[index] == class_open]


def method_from_body(
    tokens,
    brace_pairs,
    paren_pairs,
    enclosing,
    klass: ClassInfo,
    start: int,
    body_open: int,
) -> Method | None:
    direct = direct_indices(enclosing, klass.open_index, start, body_open)
    if not direct:
        return None

    plausible = []
    paren_depth = 0
    for position, index in enumerate(direct):
        if tokens[index].value == ")":
            paren_depth -= 1
            continue
        if tokens[index].value != "(":
            continue
        if paren_depth:
            paren_depth += 1
            continue
        paren_depth += 1
        if index not in paren_pairs or paren_pairs[index] >= body_open:
            continue
        previous = direct[position - 1] if position else None
        if previous is not None and IDENT.fullmatch(tokens[previous].value):
            if tokens[previous].value not in NON_NAMES:
                plausible.append((index, paren_pairs[index], previous))
        operator = next(
            (candidate for candidate in reversed(direct[:position]) if tokens[candidate].value == "operator"),
            None,
        )
        if operator is not None:
            plausible.append((index, paren_pairs[index], operator))
    if not plausible:
        return None

    direct_colons = [index for index in direct if tokens[index].value == ":"]
    first_colon = direct_colons[0] if direct_colons else None
    before_colon = [item for item in plausible if first_colon is None or item[0] < first_colon]
    parameter_open, parameter_close, name_start = (before_colon or plausible)[-1]

    if tokens[name_start].value != "operator" and name_start > start and tokens[name_start - 1].value == "~":
        name_start -= 1

    for index in direct:
        if index >= name_start:
            break
        if tokens[index].value == "friend":
            return None
        if tokens[index].value == "=":
            return None

    paren_depth = 0
    bracket_depth = 0
    for index in range(parameter_close + 1, body_open):
        if tokens[index].value == "(":
            paren_depth += 1
        elif tokens[index].value == ")":
            paren_depth -= 1
        elif tokens[index].value == "[":
            bracket_depth += 1
        elif tokens[index].value == "]":
            bracket_depth -= 1
    if paren_depth or bracket_depth:
        return None

    initializer_colon = next((index for index in direct_colons if index > parameter_close), None)
    if initializer_colon is not None:
        previous = direct[-1]
        if previous > initializer_colon and (
            IDENT.fullmatch(tokens[previous].value) or tokens[previous].value in (">", "]")
        ):
            return None

    close = brace_pairs.get(body_open)
    if close is None or tokens[start].line == tokens[close].line:
        return None
    return Method(
        klass=klass,
        start_index=start,
        body_open_index=body_open,
        body_close_index=close,
        name_start_index=name_start,
        parameter_open_index=parameter_open,
        parameter_close_index=parameter_close,
        initializer_colon_index=initializer_colon,
    )


def find_methods(text: str):
    tokens = lex(text)
    brace_pairs, paren_pairs, enclosing, parent, classes, class_names = structural_info(text, tokens)
    methods = []
    skipped_templates = []
    skipped_anonymous = []
    for class_open, klass in classes.items():
        if klass.local:
            continue
        boundary = class_open + 1
        cursor = class_open + 1
        while cursor < klass.close_index:
            if enclosing[cursor] != class_open:
                cursor += 1
                continue
            value = tokens[cursor].value
            if value == ";":
                boundary = cursor + 1
            elif (
                value == ":"
                and cursor > class_open
                and tokens[cursor - 1].value in ("private", "protected", "public")
            ):
                boundary = cursor + 1
            elif value == "{" and parent.get(cursor) == class_open:
                close = brace_pairs.get(cursor, cursor)
                if cursor not in class_names:
                    method = method_from_body(
                        tokens, brace_pairs, paren_pairs, enclosing, klass, boundary, cursor
                    )
                    if method is not None:
                        if klass.qualified_name is None:
                            skipped_anonymous.append(method)
                        else:
                            methods.append(method)
                        boundary = close + 1
                cursor = close
            cursor += 1
    return tokens, methods, skipped_templates, skipped_anonymous


def line_start(text: str, position: int) -> int:
    newline = text.rfind("\n", 0, position)
    return 0 if newline < 0 else newline + 1


def remove_default_arguments(tokens, method: Method, edits):
    depth = 0
    square = 0
    brace = 0
    cursor = method.parameter_open_index + 1
    while cursor < method.parameter_close_index:
        value = tokens[cursor].value
        if value == "(":
            depth += 1
        elif value == ")":
            depth -= 1
        elif value == "[":
            square += 1
        elif value == "]":
            square -= 1
        elif value == "{":
            brace += 1
        elif value == "}":
            brace -= 1
        elif value == "=" and depth == square == brace == 0:
            end = cursor + 1
            nested = [0, 0, 0]
            while end < method.parameter_close_index:
                other = tokens[end].value
                if other == "(":
                    nested[0] += 1
                elif other == ")":
                    nested[0] -= 1
                elif other == "[":
                    nested[1] += 1
                elif other == "]":
                    nested[1] -= 1
                elif other == "{":
                    nested[2] += 1
                elif other == "}":
                    nested[2] -= 1
                elif other == "," and nested == [0, 0, 0]:
                    break
                end += 1
            edits.append((tokens[cursor].start, tokens[end].start, ""))
            cursor = end
            continue
        cursor += 1


def apply_local_edits(raw: str, base: int, edits):
    for start, end, replacement in sorted(edits, reverse=True):
        raw = raw[: start - base] + replacement + raw[end - base :]
    return raw


def local_macro_events(text: str):
    events = []
    lines = text.splitlines(keepends=True)
    offset = 0
    index = 0
    while index < len(lines):
        directive = lines[index]
        end = offset + len(directive)
        while directive.rstrip("\r\n").endswith("\\") and index + 1 < len(lines):
            index += 1
            directive += lines[index]
            end += len(lines[index])
        match = re.match(r"\s*#\s*(define|undef)\s+([A-Za-z_][A-Za-z_0-9]*)", directive)
        if match:
            definition = directive.rstrip() if match.group(1) == "define" else None
            events.append((end, match.group(2), definition))
        offset = end
        index += 1
    return events


def macro_states(text: str, positions: list[int]):
    events = local_macro_events(text)
    active = {}
    states = {}
    event_index = 0
    for position in sorted(positions):
        while event_index < len(events) and events[event_index][0] <= position:
            _, name, definition = events[event_index]
            if definition is None:
                active.pop(name, None)
            else:
                active[name] = definition
            event_index += 1
        states[position] = active.copy()
    while event_index < len(events):
        _, name, definition = events[event_index]
        if definition is None:
            active.pop(name, None)
        else:
            active[name] = definition
        event_index += 1
    return states, active


def wrap_local_macros(raw: str, definition: str, active: dict, final: dict) -> str:
    required = {
        name
        for name in active
        if re.search(r"\b" + re.escape(name) + r"\b", raw)
        and final.get(name) != active[name]
    }
    while True:
        dependencies = {
            name
            for needed in required
            for name in active
            if re.search(r"\b" + re.escape(name) + r"\b", active[needed])
            and final.get(name) != active[name]
        }
        if dependencies <= required:
            break
        required |= dependencies
    if not required:
        return definition
    ordered = [name for name in active if name in required]
    prefix = []
    for name in ordered:
        prefix.extend((f"#ifdef {name}", f"#undef {name}", "#endif", active[name]))
    suffix = []
    for name in reversed(ordered):
        suffix.append(f"#undef {name}")
    for name in ordered:
        if name in final:
            suffix.append(final[name])
    return "\n".join((*prefix, definition, *suffix))


def dedent_member(raw: str, indent: str) -> str:
    if not indent:
        return raw
    lines = raw.splitlines(keepends=True)
    return "".join(line[len(indent):] if line.startswith(indent) else line for line in lines)


def has_no_return_type(tokens: list[Token], method: Method) -> bool:
    name = tokens[method.name_start_index].value
    if name == "~" or name == method.klass.name:
        return True
    if name != "operator":
        return False
    tail = tokens[method.name_start_index + 1:method.parameter_open_index]
    if not tail:
        return False
    first = tail[0].value
    return first == "::" or IDENT.fullmatch(first) is not None and first not in {
        "co_await",
        "delete",
        "new",
    }


def leading_return_start(tokens: list[Token], method: Method) -> int:
    cursor = method.start_index
    while cursor < method.name_start_index:
        value = tokens[cursor].value
        if value == "template" and cursor + 1 < method.name_start_index:
            cursor += 1
            if tokens[cursor].value != "<":
                continue
            depth = 0
            while cursor < method.name_start_index:
                if tokens[cursor].value == "<":
                    depth += 1
                elif tokens[cursor].value == ">":
                    depth -= 1
                    if depth == 0:
                        cursor += 1
                        break
                cursor += 1
            continue
        if value == "[[":
            cursor += 1
            while cursor < method.name_start_index and tokens[cursor].value != "]]":
                cursor += 1
            cursor += 1
            continue
        if value in LEADING_FUNCTION_SPECIFIERS:
            cursor += 1
            continue
        break
    return cursor


def trailing_return_insertion(tokens: list[Token], method: Method) -> int:
    paren_depth = 0
    bracket_depth = 0
    for index in range(method.parameter_close_index + 1, method.body_open_index):
        value = tokens[index].value
        if value == "(":
            paren_depth += 1
        elif value == ")":
            paren_depth -= 1
        elif value == "[":
            bracket_depth += 1
        elif value == "]":
            bracket_depth -= 1
        elif value == "requires" and paren_depth == bracket_depth == 0:
            return tokens[index].start
    return tokens[method.body_open_index].start


def render_method(
    text: str,
    tokens: list[Token],
    method: Method,
    active_macros: dict,
    final_macros: dict,
):
    first = tokens[method.start_index]
    start = line_start(text, first.start)
    indent = text[start:first.start]
    if indent.strip():
        start = first.start
        indent = ""
    body_open = tokens[method.body_open_index]
    body_close = tokens[method.body_close_index]

    declaration_end = body_open.start
    if method.initializer_colon_index is not None:
        declaration_end = tokens[method.initializer_colon_index].start
    declaration = text[start:declaration_end].rstrip()
    if declaration.endswith(" try"):
        declaration = declaration[:-4].rstrip()
    if "//" in declaration.rsplit("\n", 1)[-1]:
        declaration += "\n" + indent + ";"
    else:
        declaration += ";"

    raw = text[start:body_close.end]
    edits = [(tokens[method.name_start_index].start, tokens[method.name_start_index].start,
              method.klass.qualified_name + "::")]
    return_start = leading_return_start(tokens, method)
    has_trailing_return = any(
        tokens[index].value == "->"
        for index in range(method.parameter_close_index + 1, method.body_open_index)
    )
    move_return = (
        not has_no_return_type(tokens, method)
        and not has_trailing_return
        and return_start < method.name_start_index
        and not any(
            tokens[index].value == "__attribute__"
            for index in range(return_start, method.name_start_index)
        )
    )
    if move_return:
        return_type = text[
            tokens[return_start].start:tokens[method.name_start_index].start
        ].strip()
        edits.append((
            tokens[return_start].start,
            tokens[method.name_start_index].start,
            "auto ",
        ))
        insertion = trailing_return_insertion(tokens, method)
        edits.append((insertion, insertion, " -> " + return_type + " "))
    for index in range(method.start_index, method.name_start_index):
        if tokens[index].value in DECL_ONLY_PREFIX and not (
            move_return and index >= return_start
        ):
            edits.append((tokens[index].start, tokens[index].end, ""))
    for index in range(method.parameter_close_index + 1, method.body_open_index):
        if tokens[index].value in DECL_ONLY_SUFFIX:
            edits.append((tokens[index].start, tokens[index].end, ""))
    remove_default_arguments(tokens, method, edits)
    definition = dedent_member(apply_local_edits(raw, start, edits), indent).strip()
    if method.klass.template_prefixes:
        definition = "\n".join((*method.klass.template_prefixes, definition))
    definition = wrap_local_macros(raw, definition, active_macros, final_macros)
    return start, body_close.end, declaration, definition


def process(path: Path, write: bool) -> tuple[int, int, int]:
    text = path.read_text()
    tokens, methods, templates, anonymous = find_methods(text)
    if not write:
        for method in methods:
            token = tokens[method.start_index]
            print(f"{path}:{token.line}: {method.klass.qualified_name}")
        for method in templates:
            token = tokens[method.start_index]
            print(f"{path}:{token.line}: skipped template {method.klass.qualified_name}", file=sys.stderr)
        for method in anonymous:
            token = tokens[method.start_index]
            print(f"{path}:{token.line}: skipped anonymous class", file=sys.stderr)
        return len(methods), len(templates), len(anonymous)
    if not methods:
        return 0, len(templates), len(anonymous)

    replacements = []
    definitions = []
    positions = [tokens[method.start_index].start for method in methods]
    states, final_macros = macro_states(text, positions)
    for method in methods:
        position = tokens[method.start_index].start
        start, end, declaration, definition = render_method(
            text, tokens, method, states[position], final_macros
        )
        replacements.append((start, end, declaration))
        definitions.append(definition)
    for start, end, declaration in sorted(replacements, reverse=True):
        text = text[:start] + declaration + text[end:]
    text = text.rstrip() + "\n\n" + "\n\n".join(definitions) + "\n"
    path.write_text(text)
    return len(methods), len(templates), len(anonymous)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true")
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()
    total = templates = anonymous = 0
    for path in args.files:
        moved, skipped_templates, skipped_anonymous = process(path, args.write)
        total += moved
        templates += skipped_templates
        anonymous += skipped_anonymous
    action = "moved" if args.write else "found"
    print(f"{action} {total} methods; skipped {templates} template and {anonymous} anonymous")
    return 0 if not templates and not anonymous else 2


if __name__ == "__main__":
    raise SystemExit(main())
