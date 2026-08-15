#![feature(type_alias_impl_trait)]

use std::marker::PhantomData;

type Hidden<T> = impl Sized;

#[define_opaque(Hidden)]
fn hidden<T>() -> Hidden<T> {}

trait Convert<T> {}

struct Source<T>(PhantomData<T>);
struct Target<T>(PhantomData<T>);
struct Scope<T>(PhantomData<T>);

impl<T> Convert<Source<T>> for Target<T> {}

trait Select<Marker> {
    type Output;

    fn select(self) -> Self::Output;
}

impl<T: Convert<Source<Hidden<U>>>, U> Select<Target<T>> for Scope<U> {
    type Output = T;

    fn select(self) -> T {
        unimplemented!()
    }
}

fn check() {
    let _: Target<Hidden<()>> = Scope(PhantomData).select();
}

fn main() {}
