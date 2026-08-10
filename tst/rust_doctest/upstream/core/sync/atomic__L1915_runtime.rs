// Extracted from library/core/src/sync/atomic.rs:1915
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicPtr, Ordering};

    let some_ptr = AtomicPtr::new(&mut 5);

    let new = &mut 10;
    let mut old = some_ptr.load(Ordering::Relaxed);
    loop {
        match some_ptr.compare_exchange_weak(old, new, Ordering::SeqCst, Ordering::Relaxed) {
            Ok(_) => break,
            Err(x) => old = x,
        }
    }
}
