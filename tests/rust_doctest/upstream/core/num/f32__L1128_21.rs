// Extracted from library/core/src/num/f32.rs:1128
#![allow(unused)]
fn main() {
    let v = f32::from_bits(0x41480000);
    assert_eq!(v, 12.5);
}
