// Extracted from library/core/src/sync/atomic.rs:4414
use std::sync::atomic::AtomicBool;
use std::sync::atomic::Ordering;
use std::sync::atomic::compiler_fence;

static mut IMPORTANT_VARIABLE: usize = 0;
static IS_READY: AtomicBool = AtomicBool::new(false);

fn main() {
    unsafe { IMPORTANT_VARIABLE = 42 };
    // Marks earlier writes as being released with future relaxed stores.
    compiler_fence(Ordering::Release);
    IS_READY.store(true, Ordering::Relaxed);
}

fn signal_handler() {
    if IS_READY.load(Ordering::Relaxed) {
        // Acquires writes that were released with relaxed stores that we read from.
        compiler_fence(Ordering::Acquire);
        assert_eq!(unsafe { IMPORTANT_VARIABLE }, 42);
    }
}
