// Extracted from library/alloc/src/sync.rs:1057
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;

    let x = Arc::new(3);
    let y = Arc::clone(&x);

    // Two threads calling `Arc::into_inner` on both clones of an `Arc`:
    let x_thread = std::thread::spawn(|| Arc::into_inner(x));
    let y_thread = std::thread::spawn(|| Arc::into_inner(y));

    let x_inner_value = x_thread.join().unwrap();
    let y_inner_value = y_thread.join().unwrap();

    // One of the threads is guaranteed to receive the inner value:
    assert!(matches!(
        (x_inner_value, y_inner_value),
        (None, Some(3)) | (Some(3), None)
    ));
    // The result could also be `(None, None)` if the threads called
    // `Arc::try_unwrap(x).ok()` and `Arc::try_unwrap(y).ok()` instead.
}
