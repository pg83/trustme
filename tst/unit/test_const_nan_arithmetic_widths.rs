#![feature(f16, f128)]
const fn div16(left: f16, right: f16) -> f16 { left / right }
const fn div32(left: f32, right: f32) -> f32 { left / right }
const fn div64(left: f64, right: f64) -> f64 { left / right }
const fn div128(left: f128, right: f128) -> f128 { left / right }
const A: f16 = div16(0.0, 0.0);
const B: f32 = div32(0.0, 0.0);
const C: f64 = div64(0.0, 0.0);
const D: f128 = div128(0.0, 0.0);
fn main() {
    assert!(A.is_nan());
    assert!(B.is_nan());
    assert!(C.is_nan());
    assert!(D.is_nan());
}
