// Extracted from library/core/src/mem/mod.rs:929
#![allow(unused)]
fn main() {
    use std::cell::RefCell;
    
    let x = RefCell::new(1);
    
    let mut mutable_borrow = x.borrow_mut();
    *mutable_borrow = 1;
    
    drop(mutable_borrow); // relinquish the mutable borrow on this slot
    
    let borrow = x.borrow();
    println!("{}", *borrow);
}
