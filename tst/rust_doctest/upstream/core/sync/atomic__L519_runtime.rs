// Extracted from library/core/src/sync/atomic.rs:519
#![allow(unused)]
fn main() {
    use std::sync::atomic::AtomicBool;

    let atomic_true = AtomicBool::new(true);
    let atomic_false = AtomicBool::new(false);
}
