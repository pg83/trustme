//@ proc-macro-aux-build: numeric_token_bridge.rs
#![feature(f16, f128)]
use numeric_token_bridge::echo;
fn main() {
    assert_eq!(echo!(1.5f16).to_bits(), 1.5f16.to_bits());
    assert_eq!(echo!(1.25e-3f32).to_bits(), 1.25e-3f32.to_bits());
    assert_eq!(echo!(-6.25E+1f64).to_bits(), (-62.5f64).to_bits());
    assert_eq!(echo!(1.000000000000000000000000000001f128).to_bits(), 1.000000000000000000000000000001f128.to_bits());
    let range: std::ops::Range<f64> = echo!(0.125..0.5);
    assert_eq!((range.start.to_bits(), range.end.to_bits()), (0.125f64.to_bits(), 0.5f64.to_bits()));
}
