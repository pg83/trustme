// Extracted from library/alloc/src/sync.rs:3812
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    let unique: Box<str> = Box::from("eggplant");
    let shared: Arc<str> = Arc::from(unique);
    assert_eq!("eggplant", &shared[..]);
}
