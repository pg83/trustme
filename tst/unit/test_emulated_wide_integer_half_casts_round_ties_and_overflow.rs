//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Zmir-opt-level=0
//@ input: emulated_i128_target.toml
#![feature(f16)]

#[inline(never)]
fn unsigned(value: u128) -> u16 {
    (value as f16).to_bits()
}

#[inline(never)]
fn signed(value: i128) -> u16 {
    (value as f16).to_bits()
}

fn main() {
    for (value, bits) in [
        (0u128, 0x0000u16),
        (1, 0x3c00),
        (2049, 0x6800),
        (2051, 0x6802),
        (65504, 0x7bff),
        (65519, 0x7bff),
        (65520, 0x7c00),
        (1u128 << 100, 0x7c00),
        (u128::MAX, 0x7c00),
    ] {
        assert_eq!(unsigned(std::hint::black_box(value)), bits);
    }
    for (value, bits) in [
        (0i128, 0x0000u16),
        (-1, 0xbc00),
        (-2049, 0xe800),
        (-2051, 0xe802),
        (-65504, 0xfbff),
        (-65519, 0xfbff),
        (-65520, 0xfc00),
        (i128::MIN, 0xfc00),
        (i128::MAX, 0x7c00),
    ] {
        assert_eq!(signed(std::hint::black_box(value)), bits);
    }
}
