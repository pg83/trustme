// Extracted from library/core/src/num/f128.rs:888
#![allow(unused)]
#![feature(f128)]
fn main() {
    
    // FIXME(f16_f128): enable this once const casting works
    // assert_ne!((1f128).to_bits(), 1f128 as u128); // to_bits() is not casting!
    assert_eq!((12.5f128).to_bits(), 0x40029000000000000000000000000000);
}
