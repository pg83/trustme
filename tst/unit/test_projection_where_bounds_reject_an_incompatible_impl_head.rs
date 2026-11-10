//@ compile-fail: Failed to find an impl

use core::marker::PhantomData;

trait Bit {}
impl Bit for u8 {}

trait Factory
where
    Self::Item: Bit,
{
    type Item;
}

struct Wrapper<F>(PhantomData<F>);
trait Select<T> {}

impl<F: Factory> Select<F::Item> for Wrapper<F>
where
    F::Item: Bit,
{}

fn require<T: Select<u16>>() {}

fn incompatible<F: Factory>()
where
    F::Item: Bit,
{
    require::<Wrapper<F>>();
}

fn main() {}
