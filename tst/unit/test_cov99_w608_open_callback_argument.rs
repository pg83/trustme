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
    fn take<A, F>(&self, _: F, _: PhantomData<A>)
    where
        (): Bind<A, Value = F>,
    {}
}

#[define_opaque(Callback)]
fn register<A>() {
    Sink.take(|_| 7, PhantomData::<A>);
}

fn main() {
    register::<u16>();
}
