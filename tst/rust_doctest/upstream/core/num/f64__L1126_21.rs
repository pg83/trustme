// Extracted from library/core/src/num/f64.rs:1126
#![allow(unused)]
fn main() {
    let v = f64::from_bits(0x4029000000000000);
    assert_eq!(v, 12.5);
}
