// Extracted from library/core/src/iter/traits/iterator.rs:3776
#![allow(unused)]
fn main() {
    assert_eq!([1].iter().eq([1].iter()), true);
    assert_eq!([1].iter().eq([1, 2].iter()), false);
}
