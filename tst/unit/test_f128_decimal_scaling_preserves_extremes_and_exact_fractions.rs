#![feature(f128)]

const SMALL: f128 = 1.0e-4800;
const LARGE: f128 = 1.0e4900;
const BELOW_HALF_SUBNORMAL: f128 = 1.0e-4966;
const FRACTION: f128 = 17.625;

fn main() {
    assert!(SMALL > 0.0 && SMALL < 1.0);
    assert!(LARGE.is_finite() && LARGE > 1.0);
    assert_eq!(BELOW_HALF_SUBNORMAL.to_bits(), 0);
    assert_eq!(FRACTION.to_bits(), (141.0f128 / 8.0).to_bits());
}
