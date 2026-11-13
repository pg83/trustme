#![feature(type_alias_impl_trait)]

use std::marker::PhantomData;

type A = impl Copy;
type B<T> = impl Copy;

trait Parent<T, F> {
    fn parent(&self);
}
trait Child<T, F>: Parent<T, F> {}

#[define_opaque(B)]
fn define_b<T>(object: &dyn Child<T, A>) -> B<T> {
    object.parent();
    PhantomData::<T>
}

fn deferred<const N: usize>() {}

#[define_opaque(A)]
fn make_a() -> A {
    deferred::<{ std::mem::size_of::<B<()>>() }>();
    || {}
}

fn main() {
    let _ = make_a();
}
