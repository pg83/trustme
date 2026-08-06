// Extracted from library/core/src/iter/traits/iterator.rs:3726
#![allow(unused)]
#![feature(iter_order_by)]
fn main() {
    
    use std::cmp::Ordering;
    
    let xs = [1.0, 2.0, 3.0, 4.0];
    let ys = [1.0, 4.0, 9.0, 16.0];
    
    assert_eq!(
        xs.iter().partial_cmp_by(ys, |x, y| x.partial_cmp(&y)),
        Some(Ordering::Less)
    );
    assert_eq!(
        xs.iter().partial_cmp_by(ys, |x, y| (x * x).partial_cmp(&y)),
        Some(Ordering::Equal)
    );
    assert_eq!(
        xs.iter().partial_cmp_by(ys, |x, y| (2.0 * x).partial_cmp(&y)),
        Some(Ordering::Greater)
    );
}
