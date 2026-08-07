// Extracted from library/core/src/sync/atomic.rs:691
#![allow(unused)]
fn main() {
    use std::sync::atomic::AtomicBool;

    let some_bool = AtomicBool::new(true);
    assert_eq!(some_bool.into_inner(), true);
}
