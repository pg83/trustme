//@ compile-variant: --target=$DIR/emulated_i128_target.toml
#![feature(f128)]
fn from_bits(bits: u128) -> f128 { unsafe { std::mem::transmute(bits) } }
fn bits(value: f128) -> u128 { unsafe { std::mem::transmute(value) } }
#[inline(never)]
fn remainder(left: f128, right: f128) -> f128 { left % right }
fn main() {
    let sign = 1u128 << 127;
    assert_eq!(remainder(7.5, 2.0), 1.5);
    assert_eq!(remainder(-7.5, -2.0), -1.5);
    assert_eq!(bits(remainder(-4.0, 2.0)), sign);
    for exponent in [0u32, 1, 100, 1001, 16380] {
        let power = from_bits(((16383 + exponent) as u128) << 112);
        assert_eq!(remainder(power, 3.0), if exponent % 2 == 0 { 1.0 } else { 2.0 });
    }
    assert_eq!(bits(remainder(from_bits(37), from_bits(5))), 2);
    assert_eq!(bits(remainder(from_bits(sign | 37), from_bits(5))), sign | 2);
    assert_eq!(bits(remainder(from_bits(1), from_bits(5))), 1);
    assert_eq!(bits(remainder(from_bits((1u128 << 112) | 37), from_bits(1u128 << 112))), 37);
    let near_two = from_bits((16383u128 << 112) | ((1u128 << 112) - 1));
    let near_one = from_bits((16383u128 << 112) | 1);
    assert_eq!(bits(remainder(near_two, near_one)), (16382u128 << 112) | ((1u128 << 112) - 4));
    let mut expected = 1u32;
    for _ in 0..16494 { expected = expected * 2 % 37; }
    assert_eq!(bits(remainder(1.0, from_bits(37))), expected as u128);
    assert_eq!(remainder(2.0, f128::INFINITY), 2.0);
    assert!(remainder(f128::INFINITY, 2.0).is_nan());
    assert!(remainder(2.0, 0.0).is_nan());
    assert!(remainder(2.0, f128::NAN).is_nan());
}
