//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort -Zdump-hir
#![feature(type_alias_impl_trait)]

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

pub type Opaque<T> = impl Outer<Assoc: Bound<T>>;

#[define_opaque(Opaque)]
pub fn define<T>() -> Opaque<T> {
    ()
}

fn require<O, C: Carrier<Out = O>>(_: C) {}

pub fn check<T, C: Carrier<Out = Opaque<T>>>(value: C) {
    require::<Opaque<T>, C>(value);
}
