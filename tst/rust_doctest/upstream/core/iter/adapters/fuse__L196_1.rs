// Extracted from library/core/src/iter/adapters/fuse.rs:196
#![allow(unused)]
fn main() {
    use core::slice;
    use std::iter::Fuse;
    let iter: Fuse<slice::Iter<'_, u8>> = Default::default();
    assert_eq!(iter.len(), 0);
}
