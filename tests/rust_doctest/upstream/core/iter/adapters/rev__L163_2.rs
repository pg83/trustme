// Extracted from library/core/src/iter/adapters/rev.rs:163
#![allow(unused)]
fn main() {
    use core::slice;
    use core::iter::Rev;
    let iter: Rev<slice::Iter<'_, u8>> = Default::default();
    assert_eq!(iter.len(), 0);
}
