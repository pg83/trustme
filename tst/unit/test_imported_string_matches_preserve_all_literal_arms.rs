//@ aux-build: string_switch_exports.rs
extern crate string_switch_exports;

fn main() {
    for (value, expected) in [("", 3), ("birch", 5), ("cedar", 7), ("elm", 11), ("oak", 13)] {
        assert_eq!(string_switch_exports::classify(std::hint::black_box(value)), expected);
    }
}
