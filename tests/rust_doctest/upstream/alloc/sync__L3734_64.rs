// Extracted from library/alloc/src/sync.rs:3734
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    let mut original = [1, 2, 3];
    let original: &mut [i32] = &mut original;
    let shared: Arc<[i32]> = Arc::from(original);
    assert_eq!(&[1, 2, 3], &shared[..]);
}
