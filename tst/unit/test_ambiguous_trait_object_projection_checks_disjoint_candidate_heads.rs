//@ compile-fail: error:
use std::marker::PhantomData;

trait Local {}

trait Choice {
    type Out;
}

struct Slot<T: ?Sized>(PhantomData<T>);

impl Choice for Slot<dyn Local> {
    type Out = u8;
}

impl<T: ?Sized> Choice for Slot<T>
where u16: PartialEq<T> {
    type Out = u16;
}

fn main() {
    let _: Option<<Slot<_> as Choice>::Out> = None;
}
