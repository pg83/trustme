// Extracted from library/core/src/sync/atomic.rs:2727
#![allow(unused)]
fn main() {
    assert_eq!(*some_var.get_mut(), 10);
    *some_var.get_mut() = 5;
    assert_eq!(some_var.load(Ordering::SeqCst), 5);
}
