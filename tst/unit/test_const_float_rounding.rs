#![feature(f16, f128)]

const FLOOR_F64: f64 = (-1.5f64).floor();
const CEIL_F64: f64 = (-1.5f64).ceil();
const ROUND_F64: f64 = (-1.5f64).round();
const ROUND_TIES_EVEN_F64: f64 = 2.5f64.round_ties_even();
const TRUNC_F64: f64 = (-1.5f64).trunc();
const ROUND_TIES_EVEN_NEG_ZERO: u64 = (-0.5f64).round_ties_even().to_bits();

const FLOOR_F16: bool = (-1.5f16).floor().to_bits() == (-2.0f16).to_bits();
const CEIL_F32: bool = (-1.5f32).ceil().to_bits() == (-1.0f32).to_bits();
const ROUND_F128: bool = 1.5f128.round().to_bits() == 2.0f128.to_bits();
const TRUNC_F128: bool = (-1.5f128).trunc().to_bits() == (-1.0f128).to_bits();

#[inline(never)]
fn runtime_round_ties_even(value: f64) -> f64 {
    value.round_ties_even()
}

fn main() {
    assert_eq!(FLOOR_F64, -2.0);
    assert_eq!(CEIL_F64, -1.0);
    assert_eq!(ROUND_F64, -2.0);
    assert_eq!(ROUND_TIES_EVEN_F64, 2.0);
    assert_eq!(TRUNC_F64, -1.0);
    assert_eq!(ROUND_TIES_EVEN_NEG_ZERO, (-0.0f64).to_bits());
    assert!(FLOOR_F16);
    assert!(CEIL_F32);
    assert!(ROUND_F128);
    assert!(TRUNC_F128);

    assert_eq!(runtime_round_ties_even(2.5), 2.0);
    assert_eq!(runtime_round_ties_even(3.5), 4.0);
}
