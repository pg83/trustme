// Extracted from library/alloc/src/sync.rs:3754
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    let shared: Arc<str> = Arc::from("eggplant");
    assert_eq!("eggplant", &shared[..]);
}
