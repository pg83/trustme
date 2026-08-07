// Extracted from library/core/src/iter/traits/iterator.rs:3190
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    let b: [u32; 0] = [];

    assert_eq!(a.into_iter().min(), Some(1));
    assert_eq!(b.into_iter().min(), None);
}
