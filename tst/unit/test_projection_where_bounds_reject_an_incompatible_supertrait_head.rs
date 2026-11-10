//@ compile-fail: Failed to find an impl

trait Bit {}
impl Bit for u8 {}

trait Select<T> {}

trait Factory: Select<Self::Item>
where
    Self::Item: Bit,
{
    type Item;
}

fn require<T: Select<u16>>() {}

fn incompatible<F: Factory>()
where
    F::Item: Bit,
{
    require::<F>();
}

fn main() {}
