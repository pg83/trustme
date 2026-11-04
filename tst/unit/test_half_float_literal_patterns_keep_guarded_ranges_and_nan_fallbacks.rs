//@ compile-variant: -O
#![feature(f16)]

#[inline(never)]
fn classify(value: f16, enabled: bool) -> u8 {
    match value {
        -0.0f16 if enabled => 3,
        -2.0f16..=1.5f16 if enabled => 5,
        1.5f16.. if enabled => 7,
        _ => 11,
    }
}

fn main() {
    for enabled in [false, true] {
        for (value, selected) in [
            (-0.0f16, 3), (0.0, 3), (-2.0, 5), (1.5, 5),
            (2.0, 7), (f16::INFINITY, 7), (-3.0, 11),
            (f16::NEG_INFINITY, 11), (f16::NAN, 11),
        ] {
            assert_eq!(classify(std::hint::black_box(value), enabled), if enabled { selected } else { 11 });
        }
    }
}
