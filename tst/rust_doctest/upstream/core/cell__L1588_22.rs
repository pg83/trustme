// Extracted from library/core/src/cell.rs:1588
#![allow(unused)]
fn main() {
    use std::cell::{Ref, RefCell};

    let cell = RefCell::new([1, 2, 3, 4]);
    let borrow = cell.borrow();
    let (begin, end) = Ref::map_split(borrow, |slice| slice.split_at(2));
    assert_eq!(*begin, [1, 2]);
    assert_eq!(*end, [3, 4]);
}
