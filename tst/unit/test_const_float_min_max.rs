#![feature(f16, f128)]

const MIN_F64: f64 = (-9.0f64).min(0.0);
const MAX_F64: f64 = (-9.0f64).max(0.0);
const MIN_NAN_LEFT_F64: f64 = f64::NAN.min(9.0);
const MIN_NAN_RIGHT_F64: f64 = 9.0f64.min(f64::NAN);
const MAX_NAN_LEFT_F32: f32 = f32::NAN.max(9.0);
const MAX_NAN_RIGHT_F32: f32 = 9.0f32.max(f32::NAN);
const MIN_F16: bool = (-9.0f16).min(0.0).to_bits() == (-9.0f16).to_bits();
const MAX_NAN_F16: bool = 9.0f16.max(f16::NAN).to_bits() == 9.0f16.to_bits();
const MIN_F128: bool = (-9.0f128).min(0.0).to_bits() == (-9.0f128).to_bits();
const MAX_NAN_F128: bool = 9.0f128.max(f128::NAN).to_bits() == 9.0f128.to_bits();

#[inline(never)]
fn runtime_min(lhs: f64, rhs: f64) -> f64 {
    lhs.min(rhs)
}

#[inline(never)]
fn runtime_max(lhs: f32, rhs: f32) -> f32 {
    lhs.max(rhs)
}

fn main() {
    assert_eq!(MIN_F64, -9.0);
    assert_eq!(MAX_F64, 0.0);
    assert_eq!(MIN_NAN_LEFT_F64, 9.0);
    assert_eq!(MIN_NAN_RIGHT_F64, 9.0);
    assert_eq!(MAX_NAN_LEFT_F32, 9.0);
    assert_eq!(MAX_NAN_RIGHT_F32, 9.0);
    assert!(MIN_F16);
    assert!(MAX_NAN_F16);
    assert!(MIN_F128);
    assert!(MAX_NAN_F128);

    assert_eq!(runtime_min(f64::NAN, 9.0), 9.0);
    assert_eq!(runtime_min(9.0, f64::NAN), 9.0);
    assert_eq!(runtime_max(f32::NAN, 9.0), 9.0);
    assert_eq!(runtime_max(9.0, f32::NAN), 9.0);
}
