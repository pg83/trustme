#![feature(exclusive_range_pattern)]

fn pick(value: (i16, bool)) -> u8 {
    match value {
        (-3, false) => 11,
        (-8..-3, true) => 13,
        (-3..2, false) => 17,
        (2, _) => 19,
        _ => 23,
    }
}

fn main() {
    for (value, expected) in [
        ((-8, true), 13),
        ((-4, true), 13),
        ((-3, true), 23),
        ((-3, false), 11),
        ((-2, false), 17),
        ((1, false), 17),
        ((2, false), 19),
        ((2, true), 19),
        ((3, false), 23),
    ] {
        assert_eq!(pick(core::hint::black_box(value)), expected);
    }
}
