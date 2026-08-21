//@ crate-type: lib

#![feature(impl_trait_in_assoc_type)]

use core::future::Future;
use core::marker::PhantomData;

trait Marker<T, const N: usize> {}

impl<U, T, const N: usize> Marker<T, N> for (PhantomData<U>, T, [u8; N]) {}

trait Trait {
    type Inner<T, const N: usize>: Marker<T, N>;
    type Outer<T, const N: usize>: Future<Output = Self::Inner<T, N>>;

    fn outer<T, const N: usize>(value: T) -> Self::Outer<T, N>;
}

struct Implementation<U>(PhantomData<U>);

impl<U> Trait for Implementation<U> {
    type Inner<T, const N: usize> = impl Marker<T, N>;
    type Outer<T, const N: usize> = impl Future<Output = Self::Inner<T, N>>;

    fn outer<T, const N: usize>(value: T) -> Self::Outer<T, N> {
        async { (PhantomData::<U>, value, [0; N]) }
    }
}
