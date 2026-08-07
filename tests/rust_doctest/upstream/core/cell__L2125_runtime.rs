// Extracted from library/core/src/cell.rs:2125
#![allow(unused)]
fn main() {
    use std::cell::UnsafeCell;

    let uc = UnsafeCell::new(5);

    let five = uc.into_inner();
}
