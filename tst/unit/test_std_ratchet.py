#!/usr/bin/env python3

import importlib.util
import tempfile
import unittest
from pathlib import Path


RATCHET = Path(__file__).parents[2] / "dev" / "std_ratchet.py"
SPEC = importlib.util.spec_from_file_location("std_ratchet", RATCHET)
std_ratchet = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(std_ratchet)


def count(source):
    """Hits the ratchet sees in one file holding `source`."""
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "probe.cpp"
        path.write_text(source)
        total, _escaped = std_ratchet.scan([str(path)])
        return total


def escaped(source):
    """Hits the ratchet exempts in one file holding `source`."""
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "probe.cpp"
        path.write_text(source)
        _total, exempt = std_ratchet.scan([str(path)])
        return exempt


class StripCodeTest(unittest.TestCase):
    def test_a_digit_separator_does_not_open_a_character_literal(self):
        # The regression: a quote between digits used to swallow the file
        # down to the next quote, hiding every banned construct in it.
        source = (
            "static constexpr size_t budget = 10'000;\n"
            "std::vector<int> a;\n"
            "std::map<int, int> b;\n"
        )
        self.assertEqual(count(source), 2)

    def test_digit_separators_in_every_base_and_shape(self):
        for literal in ("10'000", "1'000'000", "0x8000'0000'0000'0000ull",
                        "0b1010'1010", "0777'777", "1'000.5", "1.000'5",
                        "7'450'580'596'923'828'125ull", "0x7FFF'8000"):
            source = f"auto x = {literal};\nstd::vector<int> a;\n"
            self.assertEqual(count(source), 1, literal)

    def test_a_character_literal_prefix_is_not_a_digit_separator(self):
        for literal in ("L'x'", "u'x'", "U'x'", "u8'x'", "'x'"):
            source = f"auto c = {literal};\nstd::vector<int> a;\n"
            self.assertEqual(count(source), 1, literal)

    def test_banned_names_inside_literals_stay_uncounted(self):
        self.assertEqual(count('const char* s = "std::vector";\n'), 0)
        self.assertEqual(count("char c = '\\'';\nchar d = '\"';\n"), 0)
        self.assertEqual(count('auto s = "a\\\\"; std::vector<int> a;\n'), 1)

    def test_a_string_continued_over_a_line_keeps_its_contents_hidden(self):
        source = (
            'const char* s = "first std::vector \\\n'
            'second std::map";\n'
            "std::set<int> a;\n"
        )
        self.assertEqual(count(source), 1)

    def test_a_raw_string_hides_its_contents_including_quotes(self):
        self.assertEqual(count('auto s = R"(std::vector "x" \'y\')";\n'), 0)
        self.assertEqual(count('auto s = R"tag(std::map)tag";\n'), 0)
        self.assertEqual(
            count('auto s = LR"(std::map)";\nstd::vector<int> a;\n'), 1)

    def test_an_identifier_ending_in_r_is_not_a_raw_string_prefix(self):
        source = 'FOOR"std::vector"; std::map<int, int> a;\n'
        self.assertEqual(count(source), 1)

    def test_comments_hide_their_contents(self):
        self.assertEqual(count("/* std::vector\n   std::map */\n"), 0)
        self.assertEqual(count("// std::vector\nstd::map<int, int> a;\n"), 1)

    def test_a_quote_that_never_closes_costs_only_its_own_line(self):
        source = (
            "auto c = 'unterminated;\n"
            "std::vector<int> a;\n"
            "std::map<int, int> b;\n"
        )
        self.assertEqual(count(source), 2)

    def test_line_numbering_survives_every_construct(self):
        source = (
            "auto a = 10'000;\n"
            '/* comment\n   over lines */\n'
            'auto s = R"(raw\n   text)";\n'
            'auto t = "a\\\n   b";\n'
        )
        self.assertEqual(
            len(std_ratchet.strip_code(source).splitlines()),
            len(source.splitlines()))

    def test_the_escape_hatch_still_exempts_its_line(self):
        source = (
            "size_t budget = 10'000;\n"
            "std::vector<int> a; // escape: the interface is not ours\n"
        )
        self.assertEqual(count(source), 0)
        self.assertEqual(escaped(source), 1)


if __name__ == "__main__":
    unittest.main()
