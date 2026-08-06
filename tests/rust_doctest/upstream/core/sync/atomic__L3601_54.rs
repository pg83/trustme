// Extracted from library/core/src/sync/atomic.rs:3601
#![allow(unused)]
fn main() {
    let bar = 12;
    let min_foo = foo.fetch_min(bar, Ordering::SeqCst).min(bar);
    assert_eq!(min_foo, 12);
}
