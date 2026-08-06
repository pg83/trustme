// Extracted from library/core/src/num/int_macros.rs:2426
#![allow(unused)]
#![feature(bigint_helper_methods)]
fn main() {
    // Only the most significant word is signed.
    //
    
    
    // ---------
    
    
    
    
    let borrow0 = false;
    
    
    let (diff0, borrow1) = a0.borrowing_sub(b0, borrow0);
    assert_eq!(borrow1, true);
    
    
    let (diff1, overflow) = a1.borrowing_sub(b1, borrow1);
    assert_eq!(overflow, false);
}
