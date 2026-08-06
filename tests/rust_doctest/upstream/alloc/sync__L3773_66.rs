// Extracted from library/alloc/src/sync.rs:3773
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    let mut original = String::from("eggplant");
    let original: &mut str = &mut original;
    let shared: Arc<str> = Arc::from(original);
    assert_eq!("eggplant", &shared[..]);
}
