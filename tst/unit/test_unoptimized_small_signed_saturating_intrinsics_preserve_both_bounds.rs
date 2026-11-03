//@ compile-flags: -Zmir-opt-level=0
#![feature(core_intrinsics)]

#[inline(never)]
fn clamp_byte(a: i8, b: i8) -> (i8, i8) {
    (core::intrinsics::saturating_add(a, b), core::intrinsics::saturating_sub(a, b))
}

#[inline(never)]
fn clamp_word(a: i16, b: i16) -> (i16, i16) {
    (core::intrinsics::saturating_add(a, b), core::intrinsics::saturating_sub(a, b))
}

fn main() {
    for (a, b, expected) in [
        (120, 20, (127, 100)),
        (-120, -20, (-128, -100)),
        (120, -20, (100, 127)),
        (-120, 20, (-100, -128)),
        (17, 5, (22, 12)),
    ] {
        assert_eq!(clamp_byte(std::hint::black_box(a), b), expected);
    }
    for (a, b, expected) in [
        (32000, 1000, (32767, 31000)),
        (-32000, -1000, (-32768, -31000)),
        (32000, -1000, (31000, 32767)),
        (-32000, 1000, (-31000, -32768)),
        (301, 19, (320, 282)),
    ] {
        assert_eq!(clamp_word(std::hint::black_box(a), b), expected);
    }
}
