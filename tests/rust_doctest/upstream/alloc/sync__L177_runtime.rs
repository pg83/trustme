// Extracted from library/alloc/src/sync.rs:177
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let my_arc = Arc::new(());
    let my_weak = Arc::downgrade(&my_arc);
}
