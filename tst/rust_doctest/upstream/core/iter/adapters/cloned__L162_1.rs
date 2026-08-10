// Extracted from library/core/src/iter/adapters/cloned.rs:162
#![allow(unused)]
fn main() {
    use core::slice;
    use core::iter::Cloned;
    let iter: Cloned<slice::Iter<'_, u8>> = Default::default();
    assert_eq!(iter.len(), 0);
}
