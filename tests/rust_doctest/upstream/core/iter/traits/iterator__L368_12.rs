// Extracted from library/core/src/iter/traits/iterator.rs:368
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    assert_eq!(a.into_iter().nth(10), None);
}
