// Extracted from library/core/src/iter/traits/iterator.rs:3892
#![allow(unused)]
fn main() {
    assert_eq!([1].iter().gt([1].iter()), false);
    assert_eq!([1].iter().gt([1, 2].iter()), false);
    assert_eq!([1, 2].iter().gt([1].iter()), true);
    assert_eq!([1, 2].iter().gt([1, 2].iter()), false);
}
