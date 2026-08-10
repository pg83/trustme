// Extracted from library/core/src/iter/traits/iterator.rs:3913
#![allow(unused)]
fn main() {
    assert_eq!([1].iter().ge([1].iter()), true);
    assert_eq!([1].iter().ge([1, 2].iter()), false);
    assert_eq!([1, 2].iter().ge([1].iter()), true);
    assert_eq!([1, 2].iter().ge([1, 2].iter()), true);
}
