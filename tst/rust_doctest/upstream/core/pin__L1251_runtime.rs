// Extracted from library/core/src/pin.rs:1251
#![allow(unused)]
fn main() {
    use std::mem;
    use std::pin::Pin;

    fn move_pinned_ref<T>(mut a: T, mut b: T) {
        unsafe {
            let p: Pin<&mut T> = Pin::new_unchecked(&mut a);
            // This should mean the pointee `a` can never move again.
        }
        mem::swap(&mut a, &mut b); // Potential UB down the road ⚠️
        // The address of `a` changed to `b`'s stack slot, so `a` got moved even
        // though we have previously pinned it! We have violated the pinning API contract.
    }
}
