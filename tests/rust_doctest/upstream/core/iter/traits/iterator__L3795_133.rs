// Extracted from library/core/src/iter/traits/iterator.rs:3795
#![allow(unused)]
#![feature(iter_order_by)]
fn main() {
    
    let xs = [1, 2, 3, 4];
    let ys = [1, 4, 9, 16];
    
    assert!(xs.iter().eq_by(ys, |x, y| x * x == y));
}
