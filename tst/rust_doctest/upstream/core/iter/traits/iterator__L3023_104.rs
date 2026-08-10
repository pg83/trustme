// Extracted from library/core/src/iter/traits/iterator.rs:3023
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];

    assert_eq!(a.into_iter().position(|x| x == 2), Some(1));

    assert_eq!(a.into_iter().position(|x| x == 5), None);
}
