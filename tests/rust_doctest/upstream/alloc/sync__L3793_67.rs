// Extracted from library/alloc/src/sync.rs:3793
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    let unique: String = "eggplant".to_owned();
    let shared: Arc<str> = Arc::from(unique);
    assert_eq!("eggplant", &shared[..]);
}
