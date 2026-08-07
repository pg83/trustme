// Extracted from library/alloc/src/sync.rs:2394
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::{ptr, sync::Arc};
    let inner = String::from("test");
    let ptr = inner.as_ptr();

    let arc = Arc::new(inner);
    let inner = Arc::unwrap_or_clone(arc);
    // The inner value was not cloned
    assert!(ptr::eq(ptr, inner.as_ptr()));

    let arc = Arc::new(inner);
    let arc2 = arc.clone();
    let inner = Arc::unwrap_or_clone(arc);
    // Because there were 2 references, we had to clone the inner value.
    assert!(!ptr::eq(ptr, inner.as_ptr()));
    // `arc2` is the last reference, so when we unwrap it we get back
    // the original `String`.
    let inner = Arc::unwrap_or_clone(arc2);
    assert!(ptr::eq(ptr, inner.as_ptr()));
}
