use std::sync::atomic::{AtomicU32, Ordering};

pub fn stores(destination: &AtomicU32, value: u32) {
    destination.store(value, Ordering::SeqCst);
    destination.store(value, Ordering::Release);
    destination.store(value, Ordering::Relaxed);
}
