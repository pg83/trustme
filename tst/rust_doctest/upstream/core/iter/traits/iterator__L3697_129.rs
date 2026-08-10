// Extracted from library/core/src/iter/traits/iterator.rs:3697
#![allow(unused)]
fn main() {
    assert_eq!([f64::NAN].iter().partial_cmp([1.].iter()), None);
}
