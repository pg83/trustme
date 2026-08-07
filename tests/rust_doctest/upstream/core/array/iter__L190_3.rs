// Extracted from library/core/src/array/iter.rs:190
#![allow(unused)]
#![feature(array_into_iter_constructors)]
fn main() {
    use std::array::IntoIter;

    pub fn get_bytes(b: bool) -> IntoIter<i8, 4> {
        if b {
            [1, 2, 3, 4].into_iter()
        } else {
            IntoIter::empty()
        }
    }

    assert_eq!(get_bytes(true).collect::<Vec<_>>(), vec![1, 2, 3, 4]);
    assert_eq!(get_bytes(false).collect::<Vec<_>>(), vec![]);
}
