//@ compile-variant: -O
#![feature(f128)]

#[inline(never)]
fn classify(value: f128) -> u8 {
    match value {
        -1.0f128 => 13,
        -1.0000000000000000000000000000000002f128 => 17,
        -1.0000000000000000000000000000000004f128 => 19,
        _ => 23,
    }
}

fn main() {
    let base = 0x3fff0000000000000000000000000000u128;
    let sign = 1u128 << 127;
    for (bits, expected) in [(base, 13), (base + 1, 17), (base + 2, 19), (base + 3, 23)] {
        assert_eq!(classify(std::hint::black_box(f128::from_bits(bits | sign))), expected);
    }
    assert_eq!(classify(std::hint::black_box(1.0)), 23);
    assert_eq!(classify(std::hint::black_box(f128::NAN)), 23);
}
