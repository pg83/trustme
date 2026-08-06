// Extracted from library/core/src/num/wrapping.rs:28
#![allow(unused)]
fn main() {
    use std::num::Wrapping;
    
    let zero = Wrapping(0u32);
    let one = Wrapping(1u32);
    
    assert_eq!(u32::MAX, (zero - one).0);
}
