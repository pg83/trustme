// Extracted from library/core/src/pin.rs:1795
#![allow(unused)]
fn main() {
    use core::marker::PhantomPinned as Foo;
    use core::pin::{pin, Pin};
    
    fn stuff(foo: Pin<&mut Foo>) {
        // …
        let _ = foo;
    }
    
    let pinned_foo = pin!(Foo { /* … */ });
    stuff(pinned_foo);
    // or, directly:
    stuff(pin!(Foo { /* … */ }));
}
