// Extracted from library/core/src/sync/atomic.rs:877
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};

    let some_bool = AtomicBool::new(true);

    assert_eq!(some_bool.compare_exchange(true,
                                          false,
                                          Ordering::Acquire,
                                          Ordering::Relaxed),
               Ok(true));
    assert_eq!(some_bool.load(Ordering::Relaxed), false);

    assert_eq!(some_bool.compare_exchange(true, true,
                                          Ordering::SeqCst,
                                          Ordering::Acquire),
               Err(false));
    assert_eq!(some_bool.load(Ordering::Relaxed), false);
}
