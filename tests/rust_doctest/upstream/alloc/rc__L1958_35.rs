// Extracted from library/alloc/src/rc.rs:1958
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::{ptr, rc::Rc};
    let inner = String::from("test");
    let ptr = inner.as_ptr();

    let rc = Rc::new(inner);
    let inner = Rc::unwrap_or_clone(rc);
    // The inner value was not cloned
    assert!(ptr::eq(ptr, inner.as_ptr()));

    let rc = Rc::new(inner);
    let rc2 = rc.clone();
    let inner = Rc::unwrap_or_clone(rc);
    // Because there were 2 references, we had to clone the inner value.
    assert!(!ptr::eq(ptr, inner.as_ptr()));
    // `rc2` is the last reference, so when we unwrap it we get back
    // the original `String`.
    let inner = Rc::unwrap_or_clone(rc2);
    assert!(ptr::eq(ptr, inner.as_ptr()));
}
