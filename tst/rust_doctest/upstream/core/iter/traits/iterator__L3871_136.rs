// Extracted from library/core/src/iter/traits/iterator.rs:3871
#![allow(unused)]
fn main() {
    assert_eq!([1].iter().le([1].iter()), true);
    assert_eq!([1].iter().le([1, 2].iter()), true);
    assert_eq!([1, 2].iter().le([1].iter()), false);
    assert_eq!([1, 2].iter().le([1, 2].iter()), true);
}
