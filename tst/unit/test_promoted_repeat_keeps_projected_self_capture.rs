#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

use std::marker::PhantomData;

trait Count {
    const N: usize;
}

impl Count for u8 {
    const N: usize = 3;
}

impl Count for u16 {
    const N: usize = 5;
}

struct Origin<T>(PhantomData<T>);
struct Owner<T, const N: usize>(PhantomData<T>);

trait Factory {
    type Target;
}

impl<T: Count> Factory for Origin<T>
where
    [(); T::N]:,
{
    type Target = Owner<T, { T::N }>;
}

trait Measure<T> {
    fn values() -> &'static [u8];
}

impl<T: Count> Measure<T> for <Origin<T> as Factory>::Target
where
    [(); T::N]:,
    [(); T::N + 1]:,
{
    fn values() -> &'static [u8] {
        &[7u8; T::N + 1]
    }
}

fn main() {
    assert_eq!(<Owner<u8, 3> as Measure<u8>>::values(), [7; 4]);
    assert_eq!(<Owner<u16, 5> as Measure<u16>>::values(), [7; 6]);
}
