// Extracted from library/core/src/iter/adapters/flatten.rs:342
#![allow(unused)]
fn main() {
    use core::slice;
    use std::iter::Flatten;
    let iter: Flatten<slice::Iter<'_, [u8; 4]>> = Default::default();
    assert_eq!(iter.count(), 0);
}
