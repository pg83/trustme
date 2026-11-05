//@ compile-variant: -O
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

use std::marker::PhantomData;
trait Family { type Item; }
struct Source;
impl Family for Source { type Item = u16; }
struct Holder<T, const N: usize, const M: usize>(PhantomData<T>);

impl<T: Family, const N: usize> Holder<(T, T::Item), N, { N + 1 }>
where [(); N + 1]:
{
    fn read(&self) -> usize { N }
}

fn projected<T: Family, const N: usize>(
    value: Holder<(T, T::Item), N, { N + 1 }>,
) -> usize
where [(); N + 1]:
{
    value.read()
}

fn main() {
    let value = Holder::<(Source, u16), 3, 4>(PhantomData);
    assert_eq!(projected(value), 3);
    let later = Holder::<(Source, u16), _, _>(PhantomData);
    let result = later.read();
    let _: Holder<(Source, u16), 5, 6> = later;
    assert_eq!(result, 5);
}
