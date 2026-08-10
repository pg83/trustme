// Extracted from library/core/src/num/f32.rs:1080
#![allow(unused)]
fn main() {
    assert_ne!((1f32).to_bits(), 1f32 as u32); // to_bits() is not casting!
    assert_eq!((12.5f32).to_bits(), 0x41480000);
}
