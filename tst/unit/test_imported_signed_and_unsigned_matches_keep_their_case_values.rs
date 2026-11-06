//@ aux-build: literal_switch_exports.rs
extern crate literal_switch_exports;
use literal_switch_exports::{signed, unsigned};

fn main() {
    for (value, expected) in [(0, 11), (17, 13), (1u64 << 47, 19), (u64::MAX, 23)] {
        assert_eq!(unsigned(std::hint::black_box(value)), expected);
    }
    for (value, expected) in [(-31, 29), (0, 31), (1i64 << 39, 37), (i64::MIN, 41)] {
        assert_eq!(signed(std::hint::black_box(value)), expected);
    }
}
