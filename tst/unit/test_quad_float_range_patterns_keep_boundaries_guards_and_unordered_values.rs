//@ compile-variant: -O
#![feature(f128)]

#[inline(never)]
fn classify(value: f128, enabled: bool) -> u8 {
    match value {
        -8.25f128..=-4.5f128 if enabled => 31,
        -0.0f128 if enabled => 37,
        4.25f128.. if enabled => 41,
        _ => 43,
    }
}

fn main() {
    for enabled in [false, true] {
        for (value, selected) in [
            (-8.25f128, 31), (-6.0, 31), (-4.5, 31),
            (-8.5, 43), (-4.25, 43), (-0.0, 37), (0.0, 37),
            (4.0, 43), (4.25, 41), (9.5, 41),
            (f128::INFINITY, 41), (f128::NEG_INFINITY, 43), (f128::NAN, 43),
        ] {
            assert_eq!(classify(std::hint::black_box(value), enabled), if enabled { selected } else { 43 });
        }
    }
}
