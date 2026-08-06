// Extracted from library/alloc/src/sync.rs:3715
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    let original: &[i32] = &[1, 2, 3];
    let shared: Arc<[i32]> = Arc::from(original);
    assert_eq!(&[1, 2, 3], &shared[..]);
}
