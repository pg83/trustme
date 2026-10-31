#![feature(f16, f128)]
fn main() {
    let zero = 0.0f64;
    let negative_zero = -zero;
    let infinity = f64::INFINITY;
    let nan = f64::NAN;
    assert_eq!(negative_zero.to_bits(), 1u64 << 63);
    assert_eq!((-negative_zero).to_bits(), 0);
    assert_eq!(-infinity, f64::NEG_INFINITY);
    assert!((-nan).is_nan());
    let half = 1.5f16;
    let single = 2.5f32;
    let quad = 37.5f128;
    assert_eq!(-half, -1.5f16);
    assert_eq!(-single, -2.5f32);
    assert_eq!(-quad, -37.5f128);
}
