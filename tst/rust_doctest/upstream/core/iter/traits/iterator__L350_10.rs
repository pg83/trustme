// Extracted from library/core/src/iter/traits/iterator.rs:350
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    assert_eq!(a.into_iter().nth(1), Some(2));
}
