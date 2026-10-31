//@ compile-fail: Value mismatch between
use std::marker::PhantomData;
trait Family<A> { type Item; }
trait Build { type Output; }
struct Node<F, const N: usize>(PhantomData<F>);
impl<F: Family<[u8; N]>, const N: usize> Build for Node<F, N> { type Output = <F as Family<[u8; N]>>::Item; }
fn accept<F: Family<[u8; M]>, const M: usize, B: Build<Output = <F as Family<[u8; M]>>::Item>>(_: B) {}
fn invalid<F, const N: usize, const M: usize>() where F: Family<[u8; N]> + Family<[u8; M]> { accept::<F, M, _>(Node::<F, N>(PhantomData)); }
fn main() {}
