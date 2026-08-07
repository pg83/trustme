// Extracted from library/core/src/iter/traits/iterator.rs:2740
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];

    assert!(a.into_iter().all(|x| x > 0));

    assert!(!a.into_iter().all(|x| x > 2));
}
