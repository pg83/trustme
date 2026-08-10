// Extracted from library/core/src/iter/traits/iterator.rs:3621
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;

    assert_eq!([1].iter().cmp([1].iter()), Ordering::Equal);
    assert_eq!([1].iter().cmp([1, 2].iter()), Ordering::Less);
    assert_eq!([1, 2].iter().cmp([1].iter()), Ordering::Greater);
}
