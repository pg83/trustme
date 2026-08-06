// Extracted from library/core/src/num/int_macros.rs:2323
#![allow(unused)]
#![feature(bigint_helper_methods)]
fn main() {
    // Only the most significant word is signed.
    //
    
    
    // ---------
    
    
    
    
    let carry0 = false;
    
    
    let (sum0, carry1) = a0.carrying_add(b0, carry0);
    assert_eq!(carry1, true);
    
    
    let (sum1, overflow) = a1.carrying_add(b1, carry1);
    assert_eq!(overflow, false);
    
    assert_eq!((sum1, sum0), (6, 8));
}
