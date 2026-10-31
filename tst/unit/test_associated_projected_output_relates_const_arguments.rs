#![feature(generic_arg_infer)]
use std::marker::PhantomData;
trait Family { type Item<const N: usize>; }
trait Build { type Output; }
struct Seed<F, const N: usize>(PhantomData<F>);
impl<F: Family, const N: usize> Build for Seed<F, N> { type Output = F::Item<N>; }
fn accept<F: Family, const N: usize, B: Build<Output = F::Item<N>>>(_: B) -> usize { N }
fn run<F: Family>() -> usize { accept::<F, _, _>(Seed::<F, 3>(PhantomData)) }
struct Arrays;
impl Family for Arrays { type Item<const N: usize> = [u16; N]; }
fn main() { assert_eq!(run::<Arrays>(), 3); }
