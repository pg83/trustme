//@ crate-type: lib
//@ compile-flags: --emit=metadata

pub trait ConstProject<const N: usize> {
    type Out;
}

pub struct Slot<T>(pub T);

fn consume<T: ConstProject<2>>(_: Slot<<T as ConstProject<2>>::Out>, _: T) {}

pub fn bridge<T>(value: Slot<<T as ConstProject<1>>::Out>, owner: T)
where
    T: ConstProject<1> + ConstProject<2, Out = <T as ConstProject<1>>::Out>,
{
    consume(value, owner);
}
