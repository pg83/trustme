// Extracted from library/core/src/cell.rs:534
#![allow(unused)]
fn main() {
    use std::cell::Cell;
    
    let c = Cell::new(5);
    
    let five = c.get();
}
