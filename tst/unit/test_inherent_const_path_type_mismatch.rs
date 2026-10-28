//@ compile-fail: Type mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Width<T>(std::marker::PhantomData<T>);
impl<T> Width<T> { const N: usize = std::mem::size_of::<T>(); }

fn change<T, U>(value: [u8; Width::<T>::N]) -> [u8; Width::<U>::N]
where
    [(); Width::<T>::N]:,
    [(); Width::<U>::N]:,
{
    value
}

fn main() {}
