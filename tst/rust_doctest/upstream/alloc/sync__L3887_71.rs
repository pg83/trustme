// Extracted from library/alloc/src/sync.rs:3887
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    let string: Arc<str> = Arc::from("eggplant");
    let bytes: Arc<[u8]> = Arc::from(string);
    assert_eq!("eggplant".as_bytes(), bytes.as_ref());
}
