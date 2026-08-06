// Extracted from library/core/src/cell.rs:646
#![allow(unused)]
fn main() {
    use std::cell::Cell;
    
    let c = Cell::new(5);
    let five = c.take();
    
    assert_eq!(five, 5);
    assert_eq!(c.into_inner(), 0);
}
