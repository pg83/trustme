// Extracted from library/core/src/num/uint_macros.rs:2442
#![allow(unused)]
#![feature(bigint_helper_methods)]
fn main() {
    
    
    
    // ---------
    
    
    
    
    let carry0 = false;
    
    let (sum0, carry1) = a0.carrying_add(b0, carry0);
    assert_eq!(carry1, true);
    let (sum1, carry2) = a1.carrying_add(b1, carry1);
    assert_eq!(carry2, false);
    
    assert_eq!((sum1, sum0), (9, 6));
}
