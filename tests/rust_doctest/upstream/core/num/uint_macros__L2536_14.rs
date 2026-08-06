// Extracted from library/core/src/num/uint_macros.rs:2536
#![allow(unused)]
#![feature(bigint_helper_methods)]
fn main() {
    
    
    
    // ---------
    
    
    
    
    let borrow0 = false;
    
    let (diff0, borrow1) = a0.borrowing_sub(b0, borrow0);
    assert_eq!(borrow1, true);
    let (diff1, borrow2) = a1.borrowing_sub(b1, borrow1);
    assert_eq!(borrow2, false);
}
