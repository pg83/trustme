// pin-project's `#[pin_project(UnsafeUnpin)]` implements `Unpin` with
// `where PinnedFieldsOf<Wrapper<'pin, Self>>: UnsafeUnpin`. In an impl's
// where-clause `Self` is the impl's type. An impl of a trait without items,
// such as `Unpin`, is a marker impl here, and the pass that replaces `Self`
// with the impl's type did not visit marker impls: checking
// `Blah<(), PhantomPinned>: Unpin` met a bare `Self` ("Unexpected Self").
use std::marker::{PhantomData, PhantomPinned};

pub unsafe trait UnsafeUnpin {}

pub struct Wrapper<'a, T: ?Sized>(PhantomData<&'a ()>, T);

unsafe impl<T: ?Sized + UnsafeUnpin> UnsafeUnpin for Wrapper<'_, T> {}

struct Blah<T, U> {
    first: U,
    second: T,
}

impl<'pin, T, U> Unpin for Blah<T, U> where Wrapper<'pin, Self>: UnsafeUnpin {}

unsafe impl<T: Unpin, U> UnsafeUnpin for Blah<T, U> {}

fn is_unpin<T: Unpin>() {}

fn main() {
    is_unpin::<Blah<(), PhantomPinned>>();
}
