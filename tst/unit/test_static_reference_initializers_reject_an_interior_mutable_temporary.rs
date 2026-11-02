//@ compile-fail: A constant may not refer to interior mutable data
use std::sync::atomic::{AtomicU16, Ordering};
static VALUE: &AtomicU16 = &AtomicU16::new(17);
fn main() {
    assert_eq!(VALUE.fetch_add(6, Ordering::Relaxed), 17);
    assert_eq!(VALUE.load(Ordering::Relaxed), 23);
}
