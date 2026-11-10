//@ compile-fail: type annotations needed

use core::marker::PhantomData;

trait Bit {}
impl Bit for u8 {}

trait Factory
where
    Self::Item: Bit,
{
    type Item;
}

struct ByteFactory;
impl Factory for ByteFactory {
    type Item = u8;
}

struct Wrapper<F>(PhantomData<F>);
trait Select<T> {}

impl<F: Factory> Select<F::Item> for Wrapper<F>
where
    F::Item: Bit,
{}

fn require<T: Select<u16>>() {}

fn main() {
    require::<Wrapper<_>>();
}
