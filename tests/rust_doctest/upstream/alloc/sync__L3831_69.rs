// Extracted from library/alloc/src/sync.rs:3831
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    let unique: Vec<i32> = vec![1, 2, 3];
    let shared: Arc<[i32]> = Arc::from(unique);
    assert_eq!(&[1, 2, 3], &shared[..]);
}
