//@ compile-variant: -O
use std::sync::atomic::{AtomicU16, Ordering};
static STORAGE: AtomicU16 = AtomicU16::new(17);
static VALUE: &AtomicU16 = &STORAGE;
fn main() {
    assert_eq!(VALUE.fetch_add(6, Ordering::Relaxed), 17);
    assert_eq!(STORAGE.load(Ordering::Relaxed), 23);
}
