// Extracted from library/core/src/pin.rs:567
#![allow(unused)]
fn main() {
    use std::mem::ManuallyDrop;
    use std::pin::Pin;
    struct Type;
    // Pin something inside a `ManuallyDrop`. This is fine on its own.
    let mut pin: Pin<Box<ManuallyDrop<Type>>> = Box::pin(ManuallyDrop::new(Type));

    // However, creating a pinning mutable reference to the type *inside*
    // the `ManuallyDrop` is not!
    let inner: Pin<&mut Type> = unsafe {
        Pin::map_unchecked_mut(pin.as_mut(), |x| &mut **x)
    };
}
