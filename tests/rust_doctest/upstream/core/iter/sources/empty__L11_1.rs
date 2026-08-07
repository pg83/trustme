// Extracted from library/core/src/iter/sources/empty.rs:11
#![allow(unused)]
fn main() {
    use std::iter;

    // this could have been an iterator over i32, but alas, it's just not.
    let mut nope = iter::empty::<i32>();

    assert_eq!(None, nope.next());
}
