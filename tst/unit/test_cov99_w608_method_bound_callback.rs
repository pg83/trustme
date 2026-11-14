//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(type_alias_impl_trait)]

use std::marker::PhantomData;

type Callback<A> = impl Fn(A) -> u8;

trait Bind<A> {
    type Value;
}

impl<A> Bind<A> for () {
    type Value = Callback<A>;
}

struct Sink;

impl Sink {
    fn take<A, F>(&self, callback: F, _: PhantomData<A>) -> F
    where
        (): Bind<A, Value = F>,
    {
        callback
    }
}

#[define_opaque(Callback)]
fn make<A>() -> Callback<A> {
    Sink.take(|_: A| 7, PhantomData)
}

fn main() {
    assert_eq!(make::<u16>()(3), 7);
}
