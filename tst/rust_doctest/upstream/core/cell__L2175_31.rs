// Extracted from library/core/src/cell.rs:2175
#![allow(unused)]
fn main() {
    use std::cell::UnsafeCell;

    let mut val = 42;
    let uc = UnsafeCell::from_mut(&mut val);

    *uc.get_mut() -= 1;
    assert_eq!(*uc.get_mut(), 41);
}
