#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

use std::marker::PhantomData;
use std::ops::Mul;

struct Nil;
struct Cons<T, L>(PhantomData<(T, L)>);

trait Indices<const N: usize> {
    const NUM_ELEMS: usize;
}

impl<const N: usize> Indices<N> for Nil {
    const NUM_ELEMS: usize = 1;
}

impl<T, I: Indices<N>, const N: usize> Indices<N> for Cons<T, I> {
    const NUM_ELEMS: usize = I::NUM_ELEMS * N;
}

trait Concat<J> {
    type Output;
}

impl<J> Concat<J> for Nil {
    type Output = J;
}

impl<T, I: Concat<J>, J> Concat<J> for Cons<T, I> {
    type Output = Cons<T, I::Output>;
}

struct Tensor<I: Indices<N>, const N: usize>
where
    [u8; I::NUM_ELEMS]: Sized,
{
    data: [u8; I::NUM_ELEMS],
    marker: PhantomData<I>,
}

impl<I: Indices<N>, J: Indices<N>, const N: usize> Mul<Tensor<J, N>> for Tensor<I, N>
where
    I: Concat<J>,
    I::Output: Indices<N>,
    [u8; I::NUM_ELEMS]: Sized,
    [u8; J::NUM_ELEMS]: Sized,
    [u8; <I::Output as Indices<N>>::NUM_ELEMS]: Sized,
{
    type Output = Tensor<I::Output, N>;

    fn mul(self, _: Tensor<J, N>) -> Self::Output {
        Tensor {
            data: [0; <I::Output as Indices<N>>::NUM_ELEMS],
            marker: PhantomData,
        }
    }
}

fn main() {}
