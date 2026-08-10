// Extracted from library/core/src/iter/traits/iterator.rs:3831
#![allow(unused)]
fn main() {
    assert_eq!([1].iter().ne([1].iter()), false);
    assert_eq!([1].iter().ne([1, 2].iter()), true);
}
