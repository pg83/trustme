// Extracted from library/alloc/src/sync.rs:3934
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    let evens: Arc<[u8]> = (0..10).filter(|&x| x % 2 == 0)
        .collect::<Vec<_>>() // The first set of allocations happens here.
        .into(); // A second allocation for `Arc<[T]>` happens here.
    assert_eq!(&*evens, &[0, 2, 4, 6, 8]);
}
