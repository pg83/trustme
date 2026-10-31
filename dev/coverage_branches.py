#!/usr/bin/env python3
"""Rank uncovered lines in braced C++ branches using the combined LCOV report."""

import argparse
import bisect
import json
from pathlib import Path
import re
import subprocess

from comment_gate import is_char_literal, skip_quoted, skip_raw_string


def tokens(source):
    result = []
    position = 0
    word = re.compile(r"[a-zA-Z_][a-zA-Z_0-9]*|::|[^\s]")
    while position < len(source):
        if source.startswith("//", position):
            end = source.find("\n", position)
            position = len(source) if end < 0 else end
        elif source.startswith("/*", position):
            end = source.find("*/", position + 2)
            position = len(source) if end < 0 else end + 2
        elif (end := skip_raw_string(source, position)) is not None:
            position = end
        elif source[position] == '"' or (source[position] == "'" and is_char_literal(source, position)):
            position = skip_quoted(source, position, source[position])
        elif source[position].isspace():
            position += 1
        else:
            match = word.match(source, position)
            result.append((match.group(), position))
            position = match.end()
    return result


def branches(source):
    ts = tokens(source)
    pairs = {}
    stack = []
    for index, (token, _) in enumerate(ts):
        if token in ("(", "{", "["):
            stack.append(index)
        elif token in (")", "}", "]"):
            if stack and ts[stack[-1]][0] == {")": "(", "}": "{", "]": "["}[token]:
                pairs[stack.pop()] = index
    for index, (token, start) in enumerate(ts):
        opening = index + 1
        if token in ("if", "switch", "for", "while"):
            if token == "if" and opening < len(ts) and ts[opening][0] == "constexpr":
                opening += 1
            if opening not in pairs or ts[opening][0] != "(":
                continue
            opening = pairs[opening] + 1
        elif token in ("case", "default"):
            while opening < len(ts) and ts[opening][0] not in (":", "{", "}", ";"):
                if opening in pairs:
                    opening = pairs[opening]
                opening += 1
            if opening >= len(ts) or ts[opening][0] != ":":
                continue
            opening += 1
        elif token not in ("else", "do"):
            continue
        if opening not in pairs or ts[opening][0] != "{":
            continue
        end = pairs[opening]
        yield start, ts[opening][1], ts[end][1], token


def read_counts(report):
    counts = {}
    current = None
    for line in report.read_text().splitlines():
        if line.startswith("SF:"):
            current = counts.setdefault(line[3:], {})
        elif line.startswith("DA:") and current is not None:
            number, hits, *_ = line[3:].split(",")
            number = int(number)
            current[number] = current.get(number, 0) + int(hits)
    return counts


def uncovered_spans(measured, counts):
    spans = []
    current = None
    for number in measured:
        if counts[number]:
            current = None
        else:
            if current is None:
                current = {"first": number, "last": number, "missed": 0}
                spans.append(current)
            current["last"] = number
            current["missed"] += 1
    return sorted(spans, key=lambda span: (-span["missed"], span["first"]))[:4]


def rank(root, report, revision=None, minimum=8, include_partial=False):
    result = []
    for name, counts in read_counts(report).items():
        if not name.startswith("bin/rustc/") or name.endswith("_ut.cpp"):
            continue
        if revision:
            source = subprocess.check_output(["git", "show", f"{revision}:{name}"], cwd=root, text=True)
        else:
            source = (root / name).read_text()
        newlines = [-1, *[m.start() for m in re.finditer("\n", source)]]
        line_number = lambda offset: bisect.bisect_left(newlines, offset)
        measured_numbers = sorted(counts)
        candidates = []
        for start, opening, closing, kind in branches(source):
            first, last = line_number(opening), line_number(closing)
            # Boundary lines can belong to a covered condition or neighbouring
            # case. Only executable lines strictly inside the braces decide.
            measured = measured_numbers[bisect.bisect_right(measured_numbers, first):
                                        bisect.bisect_left(measured_numbers, last)]
            missed = sum(counts[number] == 0 for number in measured)
            covered = len(measured) - missed
            if missed < minimum or (covered and not include_partial):
                continue
            entry = {
                "source": name, "line": line_number(start), "end_line": last,
                "uncovered_lines": missed, "body_lines": last - first - 1,
                "condition": " ".join(source[start:opening].split()),
                "kind": kind, "start": start, "end": closing,
            }
            if include_partial:
                entry.update(covered_lines=covered, measured_lines=len(measured),
                             largest_zero_spans=uncovered_spans(measured, counts))
            candidates.append(entry)
        # Entirely uncovered outer branches subsume their children. Partial
        # blocks keep nested branches to locate the gaps within a large body.
        for entry in candidates:
            if include_partial or not any(
                other["start"] < entry["start"] and entry["end"] <= other["end"]
                for other in candidates
            ):
                result.append({k: v for k, v in entry.items() if k not in ("start", "end")})
    return sorted(result, key=lambda row: (-row["uncovered_lines"], row["source"], row["line"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument("--source-revision", help="Git revision matching the coverage binary")
    parser.add_argument("--minimum", type=int, default=8,
                        help="Minimum number of uncovered measured body lines")
    parser.add_argument("--include-partial", action="store_true",
                        help="Include partially covered blocks and their nested branches")
    parser.add_argument("--limit", type=int, default=30)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    rows = rank(args.source_root, args.report, args.source_revision, args.minimum,
                args.include_partial)
    if args.json:
        args.json.write_text(json.dumps(rows, indent=2) + "\n")
    print("Missed  Covered  Physical  Source:line  Condition" if args.include_partial
          else "Missed  Physical  Source:line  Condition")
    for row in rows[:args.limit]:
        covered = f" {row['covered_lines']:8}" if args.include_partial else ""
        print(f"{row['uncovered_lines']:6}{covered} {row['body_lines']:9}  "
              f"{row['source']}:{row['line']}  {row['condition'][:160]}")


if __name__ == "__main__":
    main()
