//@ compile-variant: -O
#![feature(f16, f128)]

const fn invert_bool(value: bool) -> bool { !value }
const fn invert_byte(value: u8) -> u8 { !value }
const fn negate_short(value: i16) -> i16 { -value }
const fn invert_wide(value: u128) -> u128 { !value }
const fn negate_half(value: f16) -> f16 { -value }
const fn negate_single(value: f32) -> f32 { -value }
const fn negate_double(value: f64) -> f64 { -value }
const fn negate_quad(value: f128) -> f128 { -value }

const VALUE: (bool, bool, u8, i16, u128, u16, u32, u64, u128) = (
    invert_bool(false), invert_bool(true), invert_byte(0x69), negate_short(117), invert_wide(1),
    negate_half(0.0).to_bits(), negate_single(0.0).to_bits(),
    negate_double(1.5).to_bits(), negate_quad(0.0).to_bits(),
);

fn main() {
    assert_eq!(VALUE, (true, false, 0x96, -117, u128::MAX - 1,
        0x8000, 0x8000_0000, 0xbff8_0000_0000_0000, 1u128 << 127));
}
