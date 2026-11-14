//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

pub trait Bound<T> {}
impl<T> Bound<T> for () {}

pub trait Outer {
    type Assoc;
}
impl Outer for () {
    type Assoc = ();
}

pub trait Carrier {
    type Out;
}
pub struct Carry<T>(T);
impl<T> Carrier for Carry<T> {
    type Out = T;
}

pub fn make<T>() -> impl Carrier<Out = impl Outer<Assoc: Bound<T>>> {
    Carry(())
}
