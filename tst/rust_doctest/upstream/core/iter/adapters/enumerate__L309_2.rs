// Extracted from library/core/src/iter/adapters/enumerate.rs:309
#![allow(unused)]
fn main() {
    use core::slice;
    use std::iter::Enumerate;
    let iter: Enumerate<slice::Iter<'_, u8>> = Default::default();
    assert_eq!(iter.len(), 0);
}
