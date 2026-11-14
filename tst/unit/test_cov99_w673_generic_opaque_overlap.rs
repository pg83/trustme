//@ compile-fail: conflicting implementations
#![feature(type_alias_impl_trait)]

type Hidden<T> = impl Sized;

#[define_opaque(Hidden)]
fn hidden<T>() -> Hidden<T> {
    std::marker::PhantomData::<T>
}

trait Missing {}
trait Selection {}
struct Wrap<T>(T);

impl<T> Selection for Wrap<T> where Hidden<T>: Missing {}
impl<T> Selection for Wrap<T> where Hidden<T>: Missing {}

fn main() {}
