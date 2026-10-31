//@ compile-fail: Type mismatch between
use std::marker::PhantomData;
trait Family<A> { type Item; }
trait Build { type Output; }
struct Node<F>(PhantomData<F>);
impl<F: Family<u8>> Build for Node<F> { type Output = <F as Family<u8>>::Item; }
fn accept<F: Family<u16>, B: Build<Output = <F as Family<u16>>::Item>>(_: B) {}
fn invalid<F: Family<u8> + Family<u16>>() { accept::<F, _>(Node::<F>(PhantomData)); }
fn main() {}
