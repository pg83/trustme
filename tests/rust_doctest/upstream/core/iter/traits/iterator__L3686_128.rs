// Extracted from library/core/src/iter/traits/iterator.rs:3686
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;

    assert_eq!([1.].iter().partial_cmp([1.].iter()), Some(Ordering::Equal));
    assert_eq!([1.].iter().partial_cmp([1., 2.].iter()), Some(Ordering::Less));
    assert_eq!([1., 2.].iter().partial_cmp([1.].iter()), Some(Ordering::Greater));
}
