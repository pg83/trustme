// Extracted from library/core/src/sync/atomic.rs:4326
#![allow(unused)]
fn main() {
    use std::sync::atomic::AtomicBool;
    use std::sync::atomic::fence;
    use std::sync::atomic::Ordering;

    // A mutual exclusion primitive based on spinlock.
    pub struct Mutex {
        flag: AtomicBool,
    }

    impl Mutex {
        pub fn new() -> Mutex {
            Mutex {
                flag: AtomicBool::new(false),
            }
        }

        pub fn lock(&self) {
            // Wait until the old value is `false`.
            while self
                .flag
                .compare_exchange_weak(false, true, Ordering::Relaxed, Ordering::Relaxed)
                .is_err()
            {}
            // This fence synchronizes-with store in `unlock`.
            fence(Ordering::Acquire);
        }

        pub fn unlock(&self) {
            self.flag.store(false, Ordering::Release);
        }
    }
}
