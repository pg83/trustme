use std::ops::RangeFull;
use std::sync::atomic::{AtomicU8, Ordering};

static RESULT: AtomicU8 = AtomicU8::new(0);

trait Choice {
    fn method(&self) -> fn();
}

impl Choice for RangeFull {
    fn method(&self) -> fn() {
        RESULT.fetch_add(10, Ordering::Relaxed);
        || { RESULT.fetch_add(3, Ordering::Relaxed); }
    }
}

impl<F: FnOnce() -> T, T> Choice for F {
    fn method(&self) -> fn() {
        RESULT.fetch_add(20, Ordering::Relaxed);
        || { RESULT.fetch_add(4, Ordering::Relaxed); }
    }
}

fn main() {
    (|| .. .method())();
    assert_eq!(RESULT.load(Ordering::Relaxed), 24);
}
