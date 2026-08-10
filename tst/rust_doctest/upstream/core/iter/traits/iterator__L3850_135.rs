// Extracted from library/core/src/iter/traits/iterator.rs:3850
#![allow(unused)]
fn main() {
    assert_eq!([1].iter().lt([1].iter()), false);
    assert_eq!([1].iter().lt([1, 2].iter()), true);
    assert_eq!([1, 2].iter().lt([1].iter()), false);
    assert_eq!([1, 2].iter().lt([1, 2].iter()), false);
}
