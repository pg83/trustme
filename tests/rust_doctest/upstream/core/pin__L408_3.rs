// Extracted from library/core/src/pin.rs:408
#![allow(unused)]
fn main() {
    use std::pin::Pin;
    use std::marker::PhantomPinned;
    use std::ptr::NonNull;
    
    /// This is a self-referential struct because `self.slice` points into `self.data`.
    struct Unmovable {
        /// Backing buffer.
        data: [u8; 64],
        /// Points at `self.data` which we know is itself non-null. Raw pointer because we can't do
        /// this with a normal reference.
        slice: NonNull<[u8]>,
        /// Suppress `Unpin` so that this cannot be moved out of a `Pin` once constructed.
        _pin: PhantomPinned,
    }
    
    impl Unmovable {
        /// Creates a new `Unmovable`.
        ///
        /// To ensure the data doesn't move we place it on the heap behind a pinning Box.
        /// Note that the data is pinned, but the `Pin<Box<Self>>` which is pinning it can
        /// itself still be moved. This is important because it means we can return the pinning
        /// pointer from the function, which is itself a kind of move!
        fn new() -> Pin<Box<Self>> {
            let res = Unmovable {
                data: [0; 64],
                // We only create the pointer once the data is in place
                // otherwise it will have already moved before we even started.
                slice: NonNull::from(&[]),
                _pin: PhantomPinned,
            };
            // First we put the data in a box, which will be its final resting place
            let mut boxed = Box::new(res);
    
            // Then we make the slice field point to the proper part of that boxed data.
            // From now on we need to make sure we don't move the boxed data.
            boxed.slice = NonNull::from(&boxed.data);
    
            // To do that, we pin the data in place by pointing to it with a pinning
            // (`Pin`-wrapped) pointer.
            //
            // `Box::into_pin` makes existing `Box` pin the data in-place without moving it,
            // so we can safely do this now *after* inserting the slice pointer above, but we have
            // to take care that we haven't performed any other semantic moves of `res` in between.
            let pin = Box::into_pin(boxed);
    
            // Now we can return the pinned (through a pinning Box) data
            pin
        }
    }
    
    let unmovable: Pin<Box<Unmovable>> = Unmovable::new();
    
    // The inner pointee `Unmovable` struct will now never be allowed to move.
    // Meanwhile, we are free to move the pointer around.
    #[allow(unused_mut)]
    let mut still_unmoved = unmovable;
    assert_eq!(still_unmoved.slice, NonNull::from(&still_unmoved.data));
    
    // We cannot mutably dereference a `Pin<Ptr>` unless the pointee is `Unpin` or we use unsafe.
    // Since our type doesn't implement `Unpin`, this will fail to compile.
    // let mut new_unmoved = Unmovable::new();
    // std::mem::swap(&mut *still_unmoved, &mut *new_unmoved);
}
