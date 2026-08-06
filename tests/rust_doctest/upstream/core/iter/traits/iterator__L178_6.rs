// Extracted from library/core/src/iter/traits/iterator.rs:178
#![allow(unused)]
fn main() {
    // an infinite iterator has no upper bound
    // and the maximum possible lower bound
    let iter = 0..;
    
    assert_eq!((usize::MAX, None), iter.size_hint());
}
