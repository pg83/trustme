// Extracted from library/alloc/src/sync.rs:2773
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Weak;

    let empty: Weak<i64> = Weak::new();
    assert!(empty.upgrade().is_none());
}
