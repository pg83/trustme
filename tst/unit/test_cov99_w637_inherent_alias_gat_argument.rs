//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

#![feature(inherent_associated_types)]

use core::marker::PhantomData;

trait Pick {
    type Output;
}

trait G {
    type Item<V>;
}

struct Carrier<T>(T);
struct Bridge<U>(PhantomData<U>);

impl<T, U> Carrier<T>
where
    T: Pick<Output = U>,
{
    type Slot = Bridge<U>;
}

impl<U> Bridge<U> {
    fn visit<T>(&self, _: &Option<T>)
    where
        T: G<Item<U> = ()>,
    {}
}

impl G for u8 {
    type Item<V> = ();
}

fn exercise<X: Pick>() {
    let value: Carrier<X>::Slot = Bridge(PhantomData);
    let mut argument = None;
    value.visit(&argument);
    argument = Some(7u8);
}
