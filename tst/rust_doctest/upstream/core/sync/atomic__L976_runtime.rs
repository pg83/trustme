// Extracted from library/core/src/sync/atomic.rs:976
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};

    let val = AtomicBool::new(false);

    let new = true;
    let mut old = val.load(Ordering::Relaxed);
    loop {
        match val.compare_exchange_weak(old, new, Ordering::SeqCst, Ordering::Relaxed) {
            Ok(_) => break,
            Err(x) => old = x,
        }
    }
}
