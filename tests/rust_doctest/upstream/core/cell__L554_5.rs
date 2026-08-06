// Extracted from library/core/src/cell.rs:554
#![allow(unused)]
fn main() {
    use std::cell::Cell;
    
    let c = Cell::new(5);
    c.update(|x| x + 1);
    assert_eq!(c.get(), 6);
}
