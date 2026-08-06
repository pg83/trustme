// Extracted from library/core/src/cell.rs:1184
#![allow(unused)]
fn main() {
    use std::cell::RefCell;
    
    let mut c = RefCell::new(5);
    *c.get_mut() += 1;
    
    assert_eq!(c, RefCell::new(6));
}
