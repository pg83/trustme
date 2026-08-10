#!/usr/bin/env python3
"""Import executable Rust Quiz questions as checked-in source/output pairs.

Usage: import.py /path/to/rust-quiz
"""

import shutil
import sys
from pathlib import Path


HERE = Path(__file__).resolve().parent
UPSTREAM = HERE / "upstream"


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: import.py /path/to/rust-quiz")
    questions = Path(sys.argv[1]).resolve() / "questions"
    if not questions.is_dir():
        raise SystemExit(f"missing Rust Quiz questions: {questions}")

    if UPSTREAM.exists():
        shutil.rmtree(UPSTREAM)
    UPSTREAM.mkdir(parents=True)

    cases = []
    for source in sorted(questions.glob("*.rs")):
        answer_file = source.with_suffix(".md")
        first_line = answer_file.read_text(errors="surrogateescape").splitlines()[0]
        if not first_line.startswith("Answer: "):
            continue
        answer = first_line.removeprefix("Answer: ")
        if answer == "error":
            continue

        destination = UPSTREAM / source.name
        shutil.copyfile(source, destination)
        destination.with_suffix(".stdout").write_text(answer)
        cases.append(source.name)

    (HERE / "cases.txt").write_text("".join(f"{case}\n" for case in cases))
    print(f"imported {len(cases)} executable Rust Quiz questions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
