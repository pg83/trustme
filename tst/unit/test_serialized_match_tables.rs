//@ aux-build: match_table_roundtrip.rs

use match_table_roundtrip::{signed, text, unsigned};

fn main() {
    for (input, output) in [(-17, 1), (6, 2), (1025, 3), (-18, 0), (1026, 0)] {
        assert_eq!(signed(std::hint::black_box(input)), output);
    }
    for (input, output) in [(2, 4), (0x1000_0000_0001, 5), (u64::MAX, 6), (3, 0)] {
        assert_eq!(unsigned(std::hint::black_box(input)), output);
    }
    for (input, output) in [("left", 7), ("right", 8), ("", 9), ("leftover", 0)] {
        assert_eq!(text(std::hint::black_box(input)), output);
    }
}
