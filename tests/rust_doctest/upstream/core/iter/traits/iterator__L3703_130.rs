// Extracted from library/core/src/iter/traits/iterator.rs:3703
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    
    assert_eq!([1.0, f64::NAN].iter().partial_cmp([2.0, f64::NAN].iter()), Some(Ordering::Less));
    assert_eq!([2.0, f64::NAN].iter().partial_cmp([1.0, f64::NAN].iter()), Some(Ordering::Greater));
    assert_eq!([f64::NAN, 1.0].iter().partial_cmp([f64::NAN, 2.0].iter()), None);
}
