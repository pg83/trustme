//@ compile-fail: error
use std::marker::PhantomData;

struct Start;
struct Middle;

trait Step {
    type Next;
}

impl Step for Start {
    type Next = Middle;
}

impl Step for Middle {
    type Next = u8;
}

trait Witness {}

impl<T, U, V> Witness for (T, PhantomData<U>, PhantomData<V>)
where
    U: Step<Next = V>,
    T: Step<Next = U>,
{}

fn observe<W: Witness>(_: W) {}

fn main() {
    observe((Start, PhantomData, PhantomData::<u16>));
}
