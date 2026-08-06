// Extracted from library/core/src/ops/arith.rs:621
#![allow(unused)]
fn main() {
    let x: f32 = 50.50;
    let y: f32 = 8.125;
    let remainder = x - (x / y).trunc() * y;
    
    // The answer to both operations is 1.75
    assert_eq!(x % y, remainder);
}
