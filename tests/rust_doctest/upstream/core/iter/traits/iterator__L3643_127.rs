// Extracted from library/core/src/iter/traits/iterator.rs:3643
#![allow(unused)]
#![feature(iter_order_by)]
fn main() {
    
    use std::cmp::Ordering;
    
    let xs = [1, 2, 3, 4];
    let ys = [1, 4, 9, 16];
    
    assert_eq!(xs.into_iter().cmp_by(ys, |x, y| x.cmp(&y)), Ordering::Less);
    assert_eq!(xs.into_iter().cmp_by(ys, |x, y| (x * x).cmp(&y)), Ordering::Equal);
    assert_eq!(xs.into_iter().cmp_by(ys, |x, y| (2 * x).cmp(&y)), Ordering::Greater);
}
