// Extracted from library/core/src/sync/atomic.rs:3555
#![allow(unused)]
fn main() {
    let bar = 42;
    let max_foo = foo.fetch_max(bar, Ordering::SeqCst).max(bar);
    assert!(max_foo == 42);
}
