// Extracted from library/core/src/pin.rs:1197
#![allow(unused)]
fn main() {
    use std::pin::Pin;
    
    let mut val: u8 = 5;
    let pinned: Pin<&mut u8> = Pin::new(&mut val);
    
    // Unwrap the pin to get the underlying mutable reference to the value. We can do
    // this because `val` doesn't care about being moved, so the `Pin` was just
    // a "facade" anyway.
    let r = Pin::into_inner(pinned);
    assert_eq!(*r, 5);
}
