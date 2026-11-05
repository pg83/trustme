//@ compile-fail: error
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Family { type Item; }
struct Holder<T, const N: usize, const M: usize>(std::marker::PhantomData<T>);

impl<T: Family, const N: usize> Holder<(T, T::Item), N, { N + 1 }>
where [(); N + 1]:
{
    fn read(&self) -> usize { N }
}

fn unresolved<T: Family, const N: usize, const M: usize>(
    value: Holder<(T, T::Item), N, M>,
) -> usize
where [(); N + 1]:
{
    value.read()
}

fn main() {}
