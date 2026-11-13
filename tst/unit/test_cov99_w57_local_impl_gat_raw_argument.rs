//@ compile-only
#![feature(impl_trait_in_bindings)]

trait Lending {
    type Item<T>;
}

impl Lending for () {
    type Item<T> = T;
}

fn main() {
    let _: impl Lending<Item<_> = u8> = ();
}
