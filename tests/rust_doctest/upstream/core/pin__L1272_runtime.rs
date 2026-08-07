// Extracted from library/core/src/pin.rs:1272
#![allow(unused)]
fn main() {
    use std::rc::Rc;
    use std::pin::Pin;

    fn move_pinned_rc<T>(mut x: Rc<T>) {
        // This should mean the pointee can never move again.
        let pin = unsafe { Pin::new_unchecked(Rc::clone(&x)) };
        {
            let p: Pin<&T> = pin.as_ref();
            // ...
        }
        drop(pin);

        let content = Rc::get_mut(&mut x).unwrap(); // Potential UB down the road ⚠️
        // Now, if `x` was the only reference, we have a mutable reference to
        // data that we pinned above, which we could use to move it as we have
        // seen in the previous example. We have violated the pinning API contract.
    }
}
