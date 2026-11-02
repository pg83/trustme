//@ compile-variant: -O
#![feature(f16, f128)]

const BITS: (u16, u16, u128, u128) = (
    (-0.0f16).to_bits(),
    (-1.5f16).to_bits(),
    (-0.0f128).to_bits(),
    (-1.5f128).to_bits(),
);

fn main() {
    assert_eq!(BITS, (0x8000, 0xbe00, 1u128 << 127, (1u128 << 127) | (0x3fffu128 << 112) | (1u128 << 111)));
}
