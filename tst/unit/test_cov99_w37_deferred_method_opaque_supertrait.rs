#![feature(type_alias_impl_trait)]

use std::marker::PhantomData;

type Hidden = impl Copy;
type Token<T> = impl Copy;

trait Parent<F, G> {
    fn touch(&self);
}
trait Child<F, T>: Parent<F, for<'a, 'b> fn(&'a T, &'b T)> {}

#[define_opaque(Token)]
fn token<T>(object: &dyn Child<Hidden, T>) -> Token<T> {
    <dyn Child<Hidden, T> as Parent<Hidden, for<'b, 'a> fn(&'a T, &'b T)>>::touch(object);
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
