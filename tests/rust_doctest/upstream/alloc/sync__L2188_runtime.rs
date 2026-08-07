// Extracted from library/alloc/src/sync.rs:2188
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let five = Arc::new(5);

    let _ = Arc::clone(&five);
}
