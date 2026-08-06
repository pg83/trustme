// Extracted from library/alloc/src/sync.rs:3950
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    let evens: Arc<[u8]> = (0..10).collect(); // Just a single allocation happens here.
    assert_eq!(&*evens, &*(0..10).collect::<Vec<_>>());
}
