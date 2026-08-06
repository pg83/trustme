// Extracted from library/core/src/sync/atomic.rs:3101
#![allow(unused)]
fn main() {
    let mut old = val.load(Ordering::Relaxed);
    loop {
        let new = old * 2;
        match val.compare_exchange_weak(old, new, Ordering::SeqCst, Ordering::Relaxed) {
            Ok(_) => break,
            Err(x) => old = x,
        }
    }
}
