// Extracted from library/core/src/iter/adapters/copied.rs:247
#![allow(unused)]
fn main() {
    use core::slice;
    use core::iter::Copied;
    let iter: Copied<slice::Iter<'_, u8>> = Default::default();
    assert_eq!(iter.len(), 0);
}
