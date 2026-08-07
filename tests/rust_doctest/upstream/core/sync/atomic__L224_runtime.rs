// Extracted from library/core/src/sync/atomic.rs:224
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicUsize, Ordering};

    static GLOBAL_THREAD_COUNT: AtomicUsize = AtomicUsize::new(0);

    // Note that Relaxed ordering doesn't synchronize anything
    // except the global thread counter itself.
    let old_thread_count = GLOBAL_THREAD_COUNT.fetch_add(1, Ordering::Relaxed);
    // Note that this number may not be true at the moment of printing
    // because some other thread may have changed static value already.
    println!("live threads: {}", old_thread_count + 1);
}
