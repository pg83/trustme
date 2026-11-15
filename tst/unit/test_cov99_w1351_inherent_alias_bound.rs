//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(inherent_associated_types, type_alias_impl_trait)]
#![allow(incomplete_features)]

trait Carrier {
    type Item;
}

struct Wrap<T>(T);
type Callback<T: Carrier<Item = U>, U> = impl FnOnce(U) -> U;

impl<T, U> Wrap<T>
where
    T: Carrier<Item = U>,
{
    type Callback = Callback<T, U>;
}

#[define_opaque(Callback)]
fn make<T, U>() -> Wrap<T>::Callback
where
    T: Carrier<Item = U>,
{
    |value| value
}

fn main() {}
