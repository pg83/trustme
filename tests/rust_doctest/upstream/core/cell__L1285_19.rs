// Extracted from library/core/src/cell.rs:1285
#![allow(unused)]
fn main() {
    use std::cell::RefCell;
    
    let c = RefCell::new(5);
    let five = c.take();
    
    assert_eq!(five, 5);
    assert_eq!(c.into_inner(), 0);
}
