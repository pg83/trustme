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

trait Finish {
    fn value() -> u8;
}

impl Finish for u8 {
    fn value() -> u8 {
        71
    }
}

trait Witness {
    fn value() -> u8;
}

impl<T, U, V> Witness for (T, PhantomData<U>, PhantomData<V>)
where
    U: Step<Next = V>,
    T: Step<Next = U>,
    V: Finish,
{
    fn value() -> u8 {
        V::value()
    }
}

fn observe<W: Witness>(_: W) -> u8 {
    W::value()
}

fn main() {
    assert_eq!(observe((Start, PhantomData, PhantomData)), 71);
}
