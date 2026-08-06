// Extracted from library/core/src/cell.rs:2226
#![allow(unused)]
fn main() {
    use std::cell::UnsafeCell;
    
    let mut c = UnsafeCell::new(5);
    *c.get_mut() += 1;
    
    assert_eq!(*c.get_mut(), 6);
}
