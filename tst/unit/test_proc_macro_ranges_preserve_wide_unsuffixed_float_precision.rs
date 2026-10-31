//@ proc-macro-aux-build: numeric_token_bridge.rs
#![feature(f128)]
use numeric_token_bridge::echo;
fn main() {
    let exclusive: std::ops::Range<f128> = echo!(1.000000000000000000000000000001..2.0);
    assert_eq!(exclusive.start.to_bits(), 1.000000000000000000000000000001f128.to_bits());
    assert_eq!(exclusive.end.to_bits(), 2.0f128.to_bits());
    let inclusive: std::ops::RangeInclusive<f128> = echo!(-1.000000000000000000000000000001..=-0.5);
    assert_eq!(inclusive.start().to_bits(), (-1.000000000000000000000000000001f128).to_bits());
    assert_eq!(inclusive.end().to_bits(), (-0.5f128).to_bits());
}
