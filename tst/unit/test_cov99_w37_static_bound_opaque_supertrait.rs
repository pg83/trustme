#![feature(type_alias_impl_trait)]

use std::marker::PhantomData;

type Hidden = impl Copy;
type Token<T> = impl Copy;

trait Parent<F, T> {
    fn touch(&self);
}
trait Child<F>: Parent<F, ()> {}

#[define_opaque(Token)]
fn token<T>(object: &'static dyn Child<Hidden>) -> Token<T>
where
    dyn Child<Hidden>: Parent<Hidden, T>,
{
    <dyn Child<Hidden> as Parent<Hidden, T>>::touch(object);
    PhantomData::<T>
}

struct Gate;
impl Gate {
    fn wait<const N: usize>(&self) {}
}

#[define_opaque(Hidden)]
fn hidden() -> Hidden {
    Gate.wait::<{ std::mem::size_of::<Token<()>>() }>();
    || {}
}

fn main() {
    let _ = hidden();
}
