// Extracted from library/core/src/cell.rs:2200
#![allow(unused)]
fn main() {
    use std::cell::UnsafeCell;

    let uc = UnsafeCell::new(5);

    let five = uc.get();
}
