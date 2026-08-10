// Extracted from library/core/src/cell.rs:1752
#![allow(unused)]
fn main() {
    use std::cell::{RefCell, RefMut};

    let cell = RefCell::new([1, 2, 3, 4]);
    let borrow = cell.borrow_mut();
    let (mut begin, mut end) = RefMut::map_split(borrow, |slice| slice.split_at_mut(2));
    assert_eq!(*begin, [1, 2]);
    assert_eq!(*end, [3, 4]);
    begin.copy_from_slice(&[4, 3]);
    end.copy_from_slice(&[2, 1]);
}
