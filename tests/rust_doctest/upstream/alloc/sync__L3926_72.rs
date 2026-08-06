// Extracted from library/alloc/src/sync.rs:3926
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    let evens: Arc<[u8]> = (0..10).filter(|&x| x % 2 == 0).collect();
    assert_eq!(&*evens, &[0, 2, 4, 6, 8]);
}
