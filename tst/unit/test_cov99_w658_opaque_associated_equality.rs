//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort -Zdump-hir

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

pub fn make<T>() -> impl Outer<Assoc: Bound<T>> {
    ()
}

fn require_same<O, C: Carrier<Out = O>>(_: C, _: &O) {}

pub fn check<T>() {
    let first = make::<T>();
    let second = make::<T>();
    require_same(Carry(first), &second);
}
